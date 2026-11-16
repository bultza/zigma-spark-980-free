/* Verify the Puron settings adapter without vendor processes or robot IPC. */
#define main observer_program_main
#include "../src/zigma_observe.c"
#undef main
#include <assert.h>
int main(void){
 assert(puron_app_hash("784fbcda505db72675bf01a82f5b02f6eca8cb313a7916176ed7bbf10d7dd912"));
 assert(!puron_app_hash("4cf0709919f0e894a5d3ed1c60ca9d6bf11d7c47120077e23f6c13902c5d9a9a"));
 assert(!puron_app_hash("0000000000000000000000000000000000000000000000000000000000000000"));
 char path[]="/tmp/zigma-puron-settings-XXXXXX";int fd=mkstemp(path);assert(fd>=0);
 /* Captured layout: initialized cache, continuation on, obstacles on, volume 0. */
 uint32_t initialized=1;int32_t words[24]={4,1,11,91,1,0,0,1,1};unsigned char volume=0;
 assert(pwrite(fd,&initialized,4,0x49870)==4);
 assert(pwrite(fd,words,sizeof words,0x48980)==sizeof words);
 assert(pwrite(fd,&volume,1,0x48ab6)==1);
 cJSON *s=cJSON_Parse("{\"observed_at\":123,\"telemetry_source\":\"app_server_log\"}");
 assert(cached_puron_settings(s,fd));assert(number(s,"volume")==0);
 assert(cJSON_IsTrue(get(s,"continue_clean"))&&cJSON_IsTrue(get(s,"over_ridge")));
 assert(!get(s,"cache_read_at")&&!get(s,"local_link_received_at"));
 assert(number(s,"observed_at")==123&&!strcmp(get(s,"telemetry_source")->valuestring,"app_server_log"));
 assert(!strcmp(get(s,"robot_settings_source")->valuestring,"puron_app_server_cache"));
 assert(number(s,"robot_settings_read_at")>0);
 volume=100;words[4]=0;words[7]=0;
 assert(pwrite(fd,&volume,1,0x48ab6)==1);assert(pwrite(fd,words,sizeof words,0x48980)==sizeof words);
 assert(cached_puron_settings(s,fd));assert(number(s,"volume")==100);
 assert(cJSON_IsFalse(get(s,"continue_clean"))&&cJSON_IsFalse(get(s,"over_ridge")));
 cJSON_Delete(s);s=cJSON_CreateObject();
 volume=101;assert(pwrite(fd,&volume,1,0x48ab6)==1);assert(!cached_puron_settings(s,fd));assert(!s->child);
 volume=0;words[7]=2;assert(pwrite(fd,&volume,1,0x48ab6)==1);assert(pwrite(fd,words,sizeof words,0x48980)==sizeof words);
 assert(!cached_puron_settings(s,fd));assert(!s->child);
 words[7]=0;initialized=0;assert(pwrite(fd,words,sizeof words,0x48980)==sizeof words);assert(pwrite(fd,&initialized,4,0x49870)==4);
 assert(!cached_puron_settings(s,fd));assert(!s->child);
 assert(ftruncate(fd,0)==0);assert(!cached_puron_settings(s,fd));assert(!s->child);

 initialized=1;words[0]=2;words[3]=88;words[9]=0;words[10]=0;words[11]=0;
 assert(pwrite(fd,&initialized,4,0x49870)==4);assert(pwrite(fd,words,sizeof words,0x48980)==sizeof words);
 assert(cached_puron_status(s,fd));assert(!strcmp(get(s,"working_status")->valuestring,"Standby"));
 assert(!strcmp(get(s,"error")->valuestring,"NoError"));assert(number(s,"battery_level")==88);
 assert(number(s,"cache_read_at")>0&&!get(s,"local_link_received_at"));
 assert(number(s,"observed_at")==0&&number(s,"clean_area_m2")==0&&number(s,"clean_time_s")==0);
 assert(!strcmp(get(s,"statistics_units_source")->valuestring,"puron_vendor_history_contract"));
 cJSON_ReplaceItemInObjectCaseSensitive(s,"observed_at",cJSON_CreateNumber(123));
 assert(cached_puron_status(s,fd));assert(number(s,"observed_at")==123);
 words[9]=17;words[10]=3;assert(pwrite(fd,words,sizeof words,0x48980)==sizeof words);assert(cached_puron_status(s,fd));
 assert(number(s,"clean_area_m2")==17&&number(s,"clean_time_s")==180);
 assert(number(s,"vendor_clean_area_raw")==17&&number(s,"vendor_clean_time_raw")==3);
 assert(number(s,"observed_at")==0&&!get(s,"local_link_received_at"));
 words[0]=20;assert(pwrite(fd,words,sizeof words,0x48980)==sizeof words);assert(cached_puron_status(s,fd));
 assert(!strcmp(get(s,"working_status")->valuestring,"PlanningLocation"));assert(number(s,"observed_at")==0);
 cJSON_Delete(s);s=cJSON_CreateObject();
 words[0]=999;assert(pwrite(fd,words,sizeof words,0x48980)==sizeof words);assert(!cached_puron_status(s,fd));assert(!s->child);
 words[0]=2;words[3]=101;assert(pwrite(fd,words,sizeof words,0x48980)==sizeof words);assert(!cached_puron_status(s,fd));assert(!s->child);
 words[3]=88;words[11]=256;assert(pwrite(fd,words,sizeof words,0x48980)==sizeof words);assert(!cached_puron_status(s,fd));assert(!s->child);
 words[11]=26;assert(pwrite(fd,words,sizeof words,0x48980)==sizeof words);assert(cached_puron_status(s,fd));assert(!strcmp(get(s,"error")->valuestring,"VendorError26"));
 initialized=0;assert(pwrite(fd,&initialized,4,0x49870)==4);assert(!cached_puron_status(s,fd));
 cJSON_Delete(s);close(fd);unlink(path);puts("Puron settings recognition, bounds and independent timestamps: PASS");return 0;
}
