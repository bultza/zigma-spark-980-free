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
 cJSON_Delete(o);close(fd);puts("app profiles: PASS");return 0;
}
