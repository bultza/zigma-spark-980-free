/* Synthetic state from the independently captured Issue04 cache. */
#define main observe_program_main
#include "../src/zigma_observe.c"
#undef main
#include <assert.h>
int main(void){
 const struct zigma_app_profile *p=zigma_app_profile_hash("8084000b686d071f8f0590448087ff88d71deee3e55e772c86d41f27d704516c");
 assert(p&&!strcmp(p->name,"zigma-issue04"));assert(!zigma_app_profile_hash("unknown"));
 char file[]="/tmp/zigma-app-profile-XXXXXX";int fd=mkstemp(file);assert(fd>=0);unlink(file);
 int32_t state[25]={2,1,11,56,1,1,0,1,1,0,0,0};state[24]=100;
 uint32_t initialized=1;unsigned char volume=100;
 assert(pwrite(fd,state,sizeof state,0x49c44)==sizeof state);
 assert(pwrite(fd,&initialized,4,0x4ab5c)==4);assert(pwrite(fd,&volume,1,0x49d92)==1);
 cJSON *o=cJSON_CreateObject();assert(cached_status_profile(o,fd,p));assert(number(o,"battery_level")==56);assert(number(o,"volume")==100);
 assert(!cached_status_profile(o,fd,&zigma_app_profiles[0]));assert(!cached_status_profile(o,fd,NULL));
 initialized=0;assert(pwrite(fd,&initialized,4,0x4ab5c)==4);assert(!cached_status_profile(o,fd,p));
 initialized=1;assert(pwrite(fd,&initialized,4,0x4ab5c)==4);state[3]=101;assert(pwrite(fd,state,sizeof state,0x49c44)==sizeof state);assert(!cached_status_profile(o,fd,p));

 /* Caccamora cache stays at70 while the independently reported MCU state is11. */
 p=&zigma_app_profiles[0];state[0]=70;state[1]=0;state[3]=97;state[24]=50;
 assert(pwrite(fd,state,sizeof state,p->status)==sizeof state);
 assert(pwrite(fd,&initialized,4,p->initialized)==4);
 int32_t work=11;assert(pwrite(fd,&work,4,0x48eac)==4);
 assert(cached_status_profile(o,fd,p));cached_work_state(o,fd,p);
 assert(!strcmp(get(o,"working_status")->valuestring,"AutoClean"));
 assert(number(o,"vendor_state_code")==70&&number(o,"working_state_code")==11);
 assert(number(o,"observed_at")==0);
 assert(number(get(o,"fan"),"level")==2&&number(get(o,"fan"),"percent")==50);
 work=4;assert(pwrite(fd,&work,4,0x48eac)==4);cached_work_state(o,fd,p);
 assert(!strcmp(get(o,"working_status")->valuestring,"Hibernating"));
 work=999;assert(pwrite(fd,&work,4,0x48eac)==4);cached_work_state(o,fd,p);
 assert(!strcmp(get(o,"working_status")->valuestring,"unknown"));
 replace_value(o,"working_status",cJSON_CreateString("Standby"));
 cached_work_state(o,fd,&zigma_app_profiles[2]);
 assert(!strcmp(get(o,"working_status")->valuestring,"Standby"));
 status_fan(o,99,101);assert(cJSON_IsNull(get(get(o,"fan"),"level")));assert(cJSON_IsNull(get(get(o,"fan"),"percent")));
 cJSON_Delete(o);close(fd);puts("app profiles: PASS");return 0;
}
