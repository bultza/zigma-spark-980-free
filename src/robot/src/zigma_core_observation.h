#ifndef ZIGMA_CORE_OBSERVATION_H
#define ZIGMA_CORE_OBSERVATION_H
#include "zigma_process.h"
/* Optional input to the existing single observation publisher. No IPC reads. */
static long long core_clock_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (long long)t.tv_sec*1000+t.tv_nsec/1000000;}
static cJSON *core_load(const char *dir,const char *name,size_t limit){
 char path[1024];int n=snprintf(path,sizeof path,"%s/%s",dir,name);if(n<0||(size_t)n>=sizeof path)return NULL;
 FILE *f=fopen(path,"rb");if(!f)return NULL;struct stat st;
 if(fstat(fileno(f),&st)||st.st_size<=0||(unsigned long long)st.st_size>limit){fclose(f);return NULL;}
 size_t size=(size_t)st.st_size;char *text=malloc(size+1);if(!text){fclose(f);return NULL;}
 size_t got=fread(text,1,size,f);fclose(f);text[got]=0;
 cJSON *o=got==size?cJSON_ParseWithLengthOpts(text,size+1,NULL,1):NULL;free(text);
 if(!cJSON_IsObject(o)){cJSON_Delete(o);return NULL;}return o;
}
static bool core_identity(const cJSON *health,const char *exe,long long now){
 cJSON *phase=cJSON_GetObjectItemCaseSensitive(health,"phase"),*pid=cJSON_GetObjectItemCaseSensitive(health,"pid");
 cJSON *ticks=cJSON_GetObjectItemCaseSensitive(health,"start_ticks"),*seen=cJSON_GetObjectItemCaseSensitive(health,"monotonic_ms");
 if(!exe||!cJSON_IsString(phase)||strcmp(phase->valuestring,"receiving")||!cJSON_IsNumber(pid)||!isfinite(pid->valuedouble)||pid->valuedouble<1||pid->valuedouble>2147483647||floor(pid->valuedouble)!=pid->valuedouble||!cJSON_IsNumber(ticks)||!isfinite(ticks->valuedouble)||ticks->valuedouble<=0||ticks->valuedouble>=1e16||!cJSON_IsNumber(seen)||!isfinite(seen->valuedouble)||now-seen->valuedouble<0||now-seen->valuedouble>15000)return false;
 char path[64],actual[1024]={0};snprintf(path,sizeof path,"/proc/%d/exe",pid->valueint);
 return readlink(path,actual,sizeof actual-1)>0&&!strcmp(actual,exe)&&zigma_start_ticks(pid->valueint)==(unsigned long long)ticks->valuedouble;
}
static bool core_generation(const cJSON *report,const cJSON *health){
 cJSON *pid=cJSON_GetObjectItemCaseSensitive(report,"receiver_pid"),*ticks=cJSON_GetObjectItemCaseSensitive(report,"receiver_start_ticks");
 cJSON *hp=cJSON_GetObjectItemCaseSensitive(health,"pid"),*ht=cJSON_GetObjectItemCaseSensitive(health,"start_ticks");
 return cJSON_IsNumber(pid)&&cJSON_IsNumber(ticks)&&cJSON_IsNumber(hp)&&cJSON_IsNumber(ht)&&pid->valuedouble==hp->valuedouble&&ticks->valuedouble==ht->valuedouble;
}
static bool core_merge_status(cJSON *status,const cJSON *report,const cJSON *health){
 if(!cJSON_IsObject(report)||!core_generation(report,health))return false;
 cJSON *received=cJSON_GetObjectItemCaseSensitive(report,"received_at"),*mono=cJSON_GetObjectItemCaseSensitive(report,"received_monotonic_ms");
 if(!cJSON_IsNumber(received)||!isfinite(received->valuedouble)||!cJSON_IsNumber(mono)||!isfinite(mono->valuedouble))return false;
 const char *const fields[]={"working_status","error","battery_level","clean_area_m2","clean_time_s","statistics_units_source","vendor_error_code",NULL};
 cJSON *native=cJSON_GetObjectItemCaseSensitive(status,"telemetry_source");
 bool cache=cJSON_IsString(native)&&!strcmp(native->valuestring,"app_server_cache");
 bool conflict=false;
 for(unsigned i=0;fields[i]&&i<5;i++){
  cJSON *a=cJSON_GetObjectItemCaseSensitive(status,fields[i]),*b=cJSON_GetObjectItemCaseSensitive(report,fields[i]);
  if(cache&&a&&b&&!cJSON_Compare(a,b,1))conflict=true;
 }
 replace_value(status,"core_report_matches_cache",cJSON_CreateBool(!conflict));
 replace_value(status,"core_report_received_at",cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(report,"received_at"),1));
 replace_value(status,"core_report_received_monotonic_ms",cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(report,"received_monotonic_ms"),1));
 if(conflict)return false; /* A delayed IPC snapshot must never overwrite a newer native cache. */
 for(unsigned i=0;fields[i];i++){
  cJSON *v=cJSON_GetObjectItemCaseSensitive(report,fields[i]);if(v)replace_value(status,fields[i],cJSON_Duplicate(v,1));
 }
 replace_value(status,"telemetry_source",cJSON_CreateString("local_core_ipc"));
 replace_value(status,"observed_at",cJSON_CreateNumber(0));
 replace_value(status,"cache_read_at",cJSON_CreateNumber(time(NULL)));
 replace_value(status,"fresh",cJSON_CreateBool(false));
 /* local_link_received_at remains independent native channel evidence. */
 return true;
}
static cJSON *core_raster(const char *dir,const cJSON *health){
 cJSON *raw=core_load(dir,"raster.json",262144);if(!raw)return NULL;
 if(!core_generation(raw,health)||cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(raw,"CUploadRecord"))){cJSON_Delete(raw);return NULL;}
 cJSON *id=cJSON_GetObjectItemCaseSensitive(raw,"map_id"),*history_id=cJSON_GetObjectItemCaseSensitive(raw,"HisMapId");
 if(!id&&cJSON_IsString(history_id))cJSON_AddStringToObject(raw,"map_id",history_id->valuestring);
 return raw;
}
#endif
