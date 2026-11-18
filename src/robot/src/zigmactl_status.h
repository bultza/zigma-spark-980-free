#ifndef ZIGMACTL_STATUS_H
#define ZIGMACTL_STATUS_H
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include "cJSON.h"
#ifndef ZIGMACTL_STATUS_PATH
#define ZIGMACTL_STATUS_PATH "/tmp/zigma-free/observation/status.json"
#endif
/* Read the observer snapshot only: no native IPC, HTTP, or credentials. */
static int ctl_status(bool json) {
 char raw[65537]; size_t used=0; cJSON *in=NULL;
 int fd=open(ZIGMACTL_STATUS_PATH,O_RDONLY|O_NONBLOCK|O_NOFOLLOW);
 if(fd>=0){
  struct stat st;
  if(!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size>0&&st.st_size<=65536){
   while(used<sizeof(raw)-1){ssize_t n=read(fd,raw+used,sizeof(raw)-1-used);if(n<0&&errno==EINTR)continue;if(n<=0)break;used+=(size_t)n;}
   raw[used]=0;in=cJSON_ParseWithLengthOpts(raw,used+1,NULL,1);
  }
  close(fd);
 }
 bool available=cJSON_IsObject(in)&&cJSON_IsString(cJSON_GetObjectItemCaseSensitive(in,"working_status"));
 cJSON *out=cJSON_CreateObject();
 cJSON_AddNumberToObject(out,"schema_version",1);
 cJSON_AddBoolToObject(out,"available",available);
 const char *strings[]={"working_status","error", "telemetry_source",NULL};
 for(int i=0;strings[i];i++){
  cJSON *v=cJSON_GetObjectItemCaseSensitive(in,strings[i]);
  cJSON_AddStringToObject(out,strings[i],cJSON_IsString(v)?v->valuestring:"unknown");
 }
 const char *numbers[]={"battery_level","clean_time_s","clean_area_m2","observed_at",NULL};
 for(int i=0;numbers[i];i++){
  cJSON *v=cJSON_GetObjectItemCaseSensitive(in,numbers[i]);
  bool valid=cJSON_IsNumber(v)&&isfinite(v->valuedouble)&&v->valuedouble>=0;
  if(i==0&&valid)valid=v->valuedouble<=100;
  if(valid)cJSON_AddNumberToObject(out,numbers[i],v->valuedouble);else cJSON_AddNullToObject(out,numbers[i]);
 }
 struct timespec now;clock_gettime(CLOCK_REALTIME,&now);
 double epoch=now.tv_sec+now.tv_nsec/1e9;
 cJSON *stamp=cJSON_GetObjectItemCaseSensitive(out,"observed_at");
 double ts=cJSON_IsNumber(stamp)?stamp->valuedouble:0,age=epoch-ts;
 bool fresh=available&&ts>0&&age>=-1&&age<=30;
 const char *quality=!available?"unavailable":ts<=0?"missing":age < -1?"clock_mismatch":age>30?"stale":"fresh";
 cJSON *cache=cJSON_GetObjectItemCaseSensitive(in,"cache_read_at"),*link=cJSON_GetObjectItemCaseSensitive(in,"local_link_received_at");
 double ca=cJSON_IsNumber(cache)?epoch-cache->valuedouble:-1,la=cJSON_IsNumber(link)?epoch-link->valuedouble:-1;
 bool cached=available&&ca>=0&&ca<=15;
 if(cached&&!fresh&&age>=-1)quality="cached";
 cJSON_AddBoolToObject(out,"cache_available",cached);
 cJSON_AddBoolToObject(out,"local_link_active",cached&&la>=0&&la<=15);
 cJSON_AddBoolToObject(out,"fresh",fresh);
 cJSON_AddStringToObject(out,"telemetry_state",quality);
 cJSON_AddNumberToObject(out,"server_time",epoch);
 if(ts>0&&age>=0)cJSON_AddNumberToObject(out,"observation_age_s",floor(age));else cJSON_AddNullToObject(out,"observation_age_s");
 if(json){char *text=cJSON_PrintUnformatted(out);if(text){puts(text);free(text);}else{cJSON_Delete(in);cJSON_Delete(out);return 1;}}
 else{
  printf("State: %s\n",cJSON_GetObjectItemCaseSensitive(out,"working_status")->valuestring);
  cJSON *battery=cJSON_GetObjectItemCaseSensitive(out,"battery_level");
  if(cJSON_IsNumber(battery))printf("Battery: %.0f%%\n",battery->valuedouble);else puts("Battery: unknown");
  printf("Error: %s\nTelemetry: %s\n",cJSON_GetObjectItemCaseSensitive(out,"error")->valuestring,quality);
  if(ts>0&&age>=0)printf("Last observation: %.0f seconds ago\n",floor(age));
  if(!fresh)puts("Values are not confirmed current.");
 }
 cJSON_Delete(in);cJSON_Delete(out);return available?0:1;
}
#endif
