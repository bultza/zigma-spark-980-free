#include "zigma_process.h"
static long long laser_now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (long long)t.tv_sec*1000+t.tv_nsec/1000000;}
static bool laser_path(char *out,size_t n,const char *name){
 if(!status_file)return false;
 const char *s=strrchr(status_file,'/');if(!s)return false;
 return snprintf(out,n,"%.*s/%s",(int)(s-status_file),status_file,name)<(int)n;
}
static cJSON *laser_response(void){
 long long now=laser_now();char p[1024];cJSON *o=NULL;
 if(laser_path(p,sizeof p,"laser-demand")){
  char *old=read_file(p,64);long long until=old?strtoll(old,NULL,10):0;free(old);
  if(until<now+4000||until>now+7000){char lease[64];snprintf(lease,sizeof lease,"%lld\n",now+6000);atomic_write(p,lease);}
  laser_path(p,sizeof p,"laser.json");char *text=read_file(p,16384);o=text?cJSON_Parse(text):NULL;free(text);
 }
 bool valid=cJSON_IsObject(o)&&cJSON_IsNumber(get(o,"read_monotonic_ms"));
 double age=valid?now-get(o,"read_monotonic_ms")->valuedouble:-1;
 int pid=num(o,"observer_pid",0);cJSON *start=get(o,"observer_start_ticks");
 valid=valid&&age>=0&&age<=3000&&cJSON_IsNumber(start)&&isfinite(start->valuedouble)&&start->valuedouble>0&&start->valuedouble<1e16&&pid>1&&zigma_start_ticks(pid)==(unsigned long long)start->valuedouble;
 cJSON *ranges=get(o,"ranges_mm");
 if(valid&&cJSON_IsTrue(get(o,"available"))){valid=cJSON_IsArray(ranges)&&cJSON_GetArraySize(ranges)==360;
  for(cJSON *v=ranges?ranges->child:NULL;v&&valid;v=v->next)valid=cJSON_IsNumber(v)&&isfinite(v->valuedouble)&&v->valuedouble>=0&&v->valuedouble<=100000&&floor(v->valuedouble)==v->valuedouble;
 }
 if(!valid){cJSON_Delete(o);o=cJSON_CreateObject();cJSON_AddBoolToObject(o,"available",false);cJSON_AddStringToObject(o,"reason",status_file?"Waiting for laser observer":"Laser is unavailable in this mode");}
 else{
  cJSON_AddNumberToObject(o,"read_age_ms",age);
  cJSON *changed=get(o,"changed_monotonic_ms");double ca=cJSON_IsNumber(changed)?now-changed->valuedouble:-1;
  if(ca>=0)cJSON_AddNumberToObject(o,"change_age_ms",ca);else cJSON_AddNullToObject(o,"change_age_ms");
  cJSON_AddBoolToObject(o,"changing",cJSON_IsTrue(get(o,"available"))&&ca>=0&&ca<=3000);
 }
 return o;
}
