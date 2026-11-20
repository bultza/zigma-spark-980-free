/* Synthetic no-go geometry only; no private robot maps or IPC. */
#define main observer_program_main
#include "../src/zigma_observe.c"
#undef main
#include <assert.h>
static cJSON *map(void){return cJSON_Parse("{\"map_origin\":[10,20],\"zones\":[]}");}
int main(void){
 char path[]="/tmp/zigma-puron-zones-XXXXXX";int fd=mkstemp(path);assert(fd>=0);
 uint32_t puron=0x50000,original=0x51000,count=1,type=3;
 unsigned char data[40]={0};float xy[8]={1,2,1,4,3,4,3,2};
 memcpy(data,&count,4);memcpy(data+4,&type,4);memcpy(data+8,xy,sizeof xy);
 assert(pwrite(fd,&puron,4,0x4989c)==4);assert(pwrite(fd,&original,4,0x49d5c)==4);
 assert(pwrite(fd,data,sizeof data,puron)==sizeof data);
 uint32_t empty=0;assert(pwrite(fd,&empty,4,original)==4);
 cJSON *p=map(),*o=map();assert(cached_zones_at(p,fd,0x4989c));assert(cached_zones_at(o,fd,0x49d5c));
 assert(cJSON_GetArraySize(get(p,"zones"))==1&&cJSON_GetArraySize(get(o,"zones"))==0);
 cJSON *zone=cJSON_GetArrayItem(get(p,"zones"),0),*point=cJSON_GetArrayItem(get(zone,"points"),0);
 assert(get(point,"x")->valuedouble==11&&get(point,"y")->valuedouble==18);
 assert(cJSON_IsFalse(get(zone,"can_clean"))&&cJSON_IsFalse(get(zone,"can_mop")));
 assert(cJSON_IsTrue(get(p,"zones_available"))&&number(p,"zones_read_at")>0);
 cJSON_Delete(p);cJSON_Delete(o);
 for(type=0;type<=3;type++){
  assert(pwrite(fd,&type,4,puron+4)==4);p=map();assert(cached_zones_at(p,fd,0x4989c));
  zone=cJSON_GetArrayItem(get(p,"zones"),0);assert(cJSON_IsTrue(get(zone,"can_clean"))==(type!=2&&type!=3));assert(cJSON_IsTrue(get(zone,"can_mop"))==(type!=1&&type!=3));cJSON_Delete(p);
 }
 type=4;assert(pwrite(fd,&type,4,puron+4)==4);p=map();assert(!cached_zones_at(p,fd,0x4989c));assert(!get(p,"zones_available"));cJSON_Delete(p);
 type=3;assert(pwrite(fd,&type,4,puron+4)==4);float invalid=NAN;assert(pwrite(fd,&invalid,4,puron+8)==4);p=map();assert(!cached_zones_at(p,fd,0x4989c));cJSON_Delete(p);
 count=65;assert(pwrite(fd,&count,4,puron)==4);p=map();assert(!cached_zones_at(p,fd,0x4989c));cJSON_Delete(p);
 count=0;assert(pwrite(fd,&count,4,puron)==4);p=map();assert(cached_zones_at(p,fd,0x4989c));assert(cJSON_GetArraySize(get(p,"zones"))==0);cJSON_Delete(p);
 assert(pwrite(fd,&empty,4,0x4989c)==4);p=map();assert(!cached_zones_at(p,fd,0x4989c));cJSON_Delete(p);
 assert(ftruncate(fd,0)==0);p=map();assert(!cached_zones_at(p,fd,0x4989c));cJSON_Delete(p);
 close(fd);unlink(path);puts("Puron/original zone addresses, geometry, flags and invalid cache checks: PASS");return 0;
}
