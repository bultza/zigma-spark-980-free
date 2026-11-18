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
#include "zigmactl_status_extra.h"
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
 cJSON *wifi=ctl_wifi(epoch),*position=ctl_position(in,epoch);
 cJSON_AddItemToObject(out,"wifi",wifi);cJSON_AddItemToObject(out,"position",position);
 if(json){char *text=cJSON_PrintUnformatted(out);if(text){puts(text);free(text);}else{cJSON_Delete(in);cJSON_Delete(out);return 1;}}
 else{
  printf("State: %s\n",cJSON_GetObjectItemCaseSensitive(out,"working_status")->valuestring);
  cJSON *battery=cJSON_GetObjectItemCaseSensitive(out,"battery_level");
  if(cJSON_IsNumber(battery))printf("Battery: %.0f%%\n",battery->valuedouble);else puts("Battery: unknown");
  printf("Error: %s\nTelemetry: %s\n",cJSON_GetObjectItemCaseSensitive(out,"error")->valuestring,quality);
  if(ts>0&&age>=0)printf("Last observation: %.0f seconds ago\n",floor(age));
  if(!fresh)puts("Values are not confirmed current.");
  cJSON *duration=nv(out,"clean_time_s"),*area=nv(out,"clean_area_m2");
  if(cJSON_IsNumber(duration))printf("Cleaning time: %.0f min %.0f s\n",floor(duration->valuedouble/60),fmod(duration->valuedouble,60));else puts("Cleaning time: unavailable");
  if(cJSON_IsNumber(area))printf("Cleaned area: %.2f m2\n",area->valuedouble);else puts("Cleaned area: unavailable");
  printf("Wi-Fi: %s",cJSON_IsTrue(nv(wifi,"connected"))?"connected":cJSON_IsFalse(nv(wifi,"connected"))?"disconnected":"unavailable");
  if(cJSON_IsNumber(nv(wifi,"signal_dbm")))printf(" (%.0f dBm)",nn(wifi,"signal_dbm"));
  if(cJSON_IsString(nv(wifi,"ipv4")))printf(" · %s",ns(wifi,"ipv4"));
  puts("");
  if(cJSON_IsTrue(nv(position,"available")))printf("Position (cached): x=%.2f, y=%.2f cells; heading=%.3f rad; map=%s; resolution=%.3f m/cell\n",nn(position,"x"),nn(position,"y"),nn(position,"heading"),ns(position,"map_id"),nn(position,"resolution_m"));
  else puts("Position: unavailable (no recent validated map pose)");
 }
 cJSON_Delete(in);cJSON_Delete(out);return available?0:1;
}
#endif
