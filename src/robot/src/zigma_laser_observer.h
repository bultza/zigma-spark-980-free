/* Build-specific, passive scan cache. No controller requests or process writes. */
#include "mbedtls/sha256.h"
static int laser_fd=-1;
static unsigned long long laser_ticks=0;
static float laser_previous[360];
static bool laser_have=false,laser_was_active=false;
static long long laser_changed=0,laser_attempt=0,laser_next_sample=0;
static unsigned laser_sequence=0;
static cJSON *laser_pose=NULL;
static bool laser_hash(pid_t pid){
 char p[64],hex[65];snprintf(p,sizeof p,"/proc/%ld/exe",(long)pid);
 int fd=open(p,O_RDONLY|O_CLOEXEC);if(fd<0)return false;
 mbedtls_sha256_context c;mbedtls_sha256_init(&c);int rc=mbedtls_sha256_starts(&c,0);unsigned char buf[16384],hash[32];ssize_t n=0;
 while(!rc&&(n=read(fd,buf,sizeof buf))>0)rc=mbedtls_sha256_update(&c,buf,(size_t)n);
 if(n<0)rc=-1;
 if(!rc)rc=mbedtls_sha256_finish(&c,hash);
 mbedtls_sha256_free(&c);close(fd);
 if(rc)return false;
 for(int i=0;i<32;i++)snprintf(hex+i*2,3,"%02x",hash[i]);
 return !strcmp(hex,"b10fc7b48d1bdb990a2c37b82f7d45fa4fd7358f428f42d58ae885aca2970255");
}
static bool laser_guard(int fd){uint32_t p=0,v=0;return memory_read(fd,0x444594,&p,4)&&p==0x45a200&&memory_read(fd,p,&v,4)&&v==0x43b4f4;}
static bool laser_copy(int fd,float *a){
 float b[360];if(!laser_guard(fd))return false;
 for(int j=0;j<3;j++){
  if(!memory_read(fd,0x45a204,a,sizeof b)||!memory_read(fd,0x45a204,b,sizeof b))return false;
  if(memcmp(a,b,sizeof b))continue;
  for(int i=0;i<360;i++)if(!isfinite(a[i])||a[i]<0||a[i]>100)return false;
  return laser_guard(fd);
 }return false;
}
static void laser_close(void){if(laser_fd>=0)close(laser_fd);laser_fd=-1;laser_ticks=0;laser_have=false;laser_changed=0;laser_sequence=0;cJSON_Delete(laser_pose);laser_pose=NULL;}
static void laser_remember(const float *values,const cJSON *pose,long long now){
 bool changed=laser_have&&memcmp(values,laser_previous,sizeof laser_previous);
 if(!laser_have||changed){
  if(changed){laser_changed=now;laser_sequence++;}
  cJSON_Delete(laser_pose);laser_pose=pose?cJSON_Duplicate(pose,1):NULL;
 }
 memcpy(laser_previous,values,sizeof laser_previous);laser_have=true;
}
static bool laser_demand(const char *dir,long long now){
 char p[1024];snprintf(p,sizeof p,"%s/laser-demand",dir);FILE *f=fopen(p,"r");if(!f)return false;
 long long until=0;int n=fscanf(f,"%lld",&until);fclose(f);return n==1&&until>now&&until<=now+7000;
}
static bool laser_tick(const char *dir,long long now){
 if(!laser_demand(dir,now)){
  if(laser_was_active){laser_close();char p[1024];snprintf(p,sizeof p,"%s/laser.json",dir);unlink(p);}
  laser_was_active=false;laser_next_sample=0;return false;
 }
 laser_was_active=true;
 if(now<laser_next_sample)return true;
 laser_next_sample=now+2000;
 unsigned long long ticks=zigma_start_ticks(nav_pid);
 if(laser_fd>=0&&(!ticks||ticks!=laser_ticks))laser_close();
 if(laser_fd<0&&now-laser_attempt>=3000){
  laser_attempt=now;char p[64];
  if(ticks&&zigma_native_process(nav_pid,"/root/hoslam/demo_rplidar")&&laser_hash(nav_pid)&&ticks==zigma_start_ticks(nav_pid)){
   snprintf(p,sizeof p,"/proc/%d/mem",nav_pid);laser_fd=open(p,O_RDONLY|O_CLOEXEC);laser_ticks=ticks;
  }
 }
 cJSON *pose_before=laser_fd>=0?cached_navigation_pose(laser_fd,map_geometry):NULL;
 float values[360];bool ok=laser_fd>=0&&laser_copy(laser_fd,values)&&laser_ticks==zigma_start_ticks(nav_pid);
 cJSON *pose_after=ok?cached_navigation_pose(laser_fd,map_geometry):NULL;
 bool pose_ok=pose_before&&pose_after&&fabs(get(pose_before,"heading")->valuedouble-get(pose_after,"heading")->valuedouble)<0.01;
 cJSON *o=cJSON_CreateObject();cJSON_AddBoolToObject(o,"available",ok);
 cJSON_AddNumberToObject(o,"read_monotonic_ms",now);cJSON_AddNumberToObject(o,"observer_pid",getpid());cJSON_AddNumberToObject(o,"observer_start_ticks",(double)zigma_start_ticks(getpid()));
 if(ok){
  laser_remember(values,pose_ok?pose_after:NULL,now);
  if(laser_pose)cJSON_AddItemToObject(o,"navigation_pose",cJSON_Duplicate(laser_pose,1));
  cJSON *ranges=cJSON_AddArrayToObject(o,"ranges_mm");for(int i=0;i<360;i++)cJSON_AddItemToArray(ranges,cJSON_CreateNumber(lroundf(values[i]*1000)));
  cJSON_AddNumberToObject(o,"sequence",laser_sequence);
  if(laser_changed)cJSON_AddNumberToObject(o,"changed_monotonic_ms",laser_changed);else cJSON_AddNullToObject(o,"changed_monotonic_ms");
  cJSON_AddNullToObject(o,"measured_at");cJSON_AddStringToObject(o,"orientation","sensor_index");
 }else{cJSON_AddStringToObject(o,"reason","Laser reader unavailable for this navigation process");laser_close();}
 cJSON_Delete(pose_before);cJSON_Delete(pose_after);
 save(dir,"laser.json",o);cJSON_Delete(o);return true;
}
