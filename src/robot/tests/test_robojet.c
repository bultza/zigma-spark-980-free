/* Synthetic sparse process memory, no original firmware or robot needed. */
#define pread tracked_pread
#define main observer_program_main
#include "../src/zigma_observe.c"
#undef main
#undef pread
#include <assert.h>
extern ssize_t pread(int,void *,size_t,off_t);
static int status_reads,wall_slot_reads,mutate;
ssize_t tracked_pread(int fd,void *buf,size_t n,off_t offset){
 if(offset==0x4395c){assert(n==92);status_reads++;}
 ssize_t got=pread(fd,buf,n,offset);
 if(offset==0x43e24&&++wall_slot_reads==3&&mutate&&got==4){uint32_t other=0x51000;memcpy(buf,&other,4);}
 return got;
}
static void put(int fd,off_t at,const void *p,size_t n){assert(pwrite(fd,p,n,at)==(ssize_t)n);}
static cJSON *map(void){return cJSON_Parse("{\"map_id\":\"a\",\"width\":100,\"height\":100,\"map_origin\":[10,20],\"zones\":[]}");}
int main(void){
 const struct zigma_app_profile *p=zigma_app_profile_hash("8e323cfe110e4aa78d680b34c6c26a4276ada7869d58f49de64c0440fa551f6c");
 assert(p&&!strcmp(p->name,"robojet-issue08")&&p->status_size==92);
 assert(!zigma_app_profile_hash("8e323cfe110e4aa78d680b34c6c26a4276ada7869d58f49de64c0440fa551f6d"));
 char file[]="/tmp/robojet-test-XXXXXX";int fd=mkstemp(file);assert(fd>=0);unlink(file);
 int32_t state[23]={2,1,11,56,1,1,0,1,1,123,4,0};uint32_t initialized=1;unsigned char volume=45;
 put(fd,p->status,state,sizeof state);put(fd,p->initialized,&initialized,4);put(fd,p->volume,&volume,1);
 cJSON *s=cJSON_CreateObject();assert(cached_status_profile(s,fd,p)&&status_reads==2);
 assert(number(s,"battery_level")==56&&number(s,"volume")==45&&number(s,"vendor_clean_area_raw")==123&&number(s,"vendor_clean_time_raw")==4);
 assert(cJSON_IsTrue(get(s,"continue_clean"))&&cJSON_IsTrue(get(s,"over_ridge")));
 assert(number(s,"clean_area_m2")==123&&number(s,"clean_time_s")==240);
 assert(!strcmp(get(s,"statistics_units_source")->valuestring,"robojet_json_contract_provisional"));
 state[9]=state[10]=0;put(fd,p->status,state,sizeof state);assert(cached_status_profile(s,fd,p));
 assert(cJSON_IsNumber(get(s,"clean_area_m2"))&&number(s,"clean_area_m2")==0&&cJSON_IsNumber(get(s,"clean_time_s"))&&number(s,"clean_time_s")==0);
 state[9]=123;put(fd,p->status,state,sizeof state);
 state[10]=65535;put(fd,p->status,state,sizeof state);assert(cached_status_profile(s,fd,p)&&number(s,"vendor_clean_time_raw")==65535&&number(s,"clean_time_s")==3932100);
 assert(!cached_status_profile(s,fd,&zigma_app_profiles[0]));
 initialized=0;put(fd,p->initialized,&initialized,4);assert(!cached_status_profile(s,fd,p));initialized=1;put(fd,p->initialized,&initialized,4);
 state[3]=101;put(fd,p->status,state,sizeof state);assert(!cached_status_profile(s,fd,p));cJSON_Delete(s);
 uint32_t wall=0x50000,polygon=0x51000,count=1;unsigned char w[20]={0},z[36]={0};float line[]={1,2,3,4},quad[]={5,6,7,8,9,10,11,12};
 memcpy(w,&count,4);memcpy(w+4,line,sizeof line);memcpy(z,&count,4);memcpy(z+4,quad,sizeof quad);
 put(fd,0x43e24,&wall,4);put(fd,0x43e28,&polygon,4);put(fd,wall,w,sizeof w);put(fd,polygon,z,sizeof z);
 cJSON *m=map();assert(cached_robojet_zones(m,fd));assert(cJSON_IsFalse(get(m,"zones_read_only")));
 cJSON *zones=get(m,"zones"),*a=cJSON_GetArrayItem(zones,0),*b=cJSON_GetArrayItem(zones,1);assert(cJSON_GetArraySize(zones)==2);
 assert(cJSON_GetArraySize(get(a,"points"))==2&&cJSON_GetArraySize(get(b,"points"))==4);
 assert(!strcmp(get(a,"kind")->valuestring,"virtual_wall")&&!strcmp(get(b,"kind")->valuestring,"forbidden_polygon"));
 assert(cJSON_IsNull(get(a,"can_clean"))&&cJSON_IsFalse(get(b,"can_mop")));
 cJSON *pt=cJSON_GetArrayItem(get(a,"points"),0);assert(number(pt,"x")==11&&number(pt,"y")==18);
 pt=cJSON_GetArrayItem(get(b,"points"),3);assert(number(pt,"x")==21&&number(pt,"y")==8);
 cJSON *old=map();assert(merge_zone_cache(old,m)&&cJSON_IsFalse(get(old,"zones_read_only")));cJSON_Delete(old);cJSON_Delete(m);
 initialized=0;put(fd,p->initialized,&initialized,4);m=map();assert(!cached_robojet_zones(m,fd));cJSON_Delete(m);initialized=1;put(fd,p->initialized,&initialized,4);
 mutate=1;wall_slot_reads=0;m=map();assert(!cached_robojet_zones(m,fd)&&!get(m,"zones_available"));cJSON_Delete(m);mutate=0;
 float bad=NAN;put(fd,polygon+4,&bad,4);m=map();assert(!cached_robojet_zones(m,fd)&&cJSON_GetArraySize(get(m,"zones"))==0);cJSON_Delete(m);put(fd,polygon,z,sizeof z);
 count=65;put(fd,wall,&count,4);m=map();assert(!cached_robojet_zones(m,fd));cJSON_Delete(m);
 count=0;put(fd,wall,&count,4);put(fd,polygon,&count,4);m=map();assert(cached_robojet_zones(m,fd)&&cJSON_GetArraySize(get(m,"zones"))==0);old=map();assert(merge_zone_cache(old,m)&&cJSON_IsFalse(get(old,"zones_read_only")));cJSON_Delete(old);cJSON_Delete(m);
 put(fd,0x43e28,&count,4);m=map();assert(!cached_robojet_zones(m,fd));cJSON_Delete(m);
 put(fd,0x43e28,&polygon,4);m=cJSON_Parse("{\"map_origin\":[10],\"zones\":[]}");assert(!cached_robojet_zones(m,fd));cJSON_Delete(m);
 assert(!ftruncate(fd,polygon+2));m=map();assert(!cached_robojet_zones(m,fd));cJSON_Delete(m);
 cJSON *unknown=map(),*known=map();cJSON_AddBoolToObject(known,"zones_available",true);
 char *sig1=zone_signature(unknown),*sig2=zone_signature(known);assert(strcmp(sig1,sig2));free(sig1);free(sig2);cJSON_Delete(unknown);cJSON_Delete(known);
 close(fd);puts("RoboJet exact-size state, split geometry, mutation, bounds and partial-read guards: PASS");return 0;
}
