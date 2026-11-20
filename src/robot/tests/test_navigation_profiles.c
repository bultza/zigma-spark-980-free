#define main observe_program_main
#include "../src/zigma_observe.c"
#undef main
#include <assert.h>
static void write_at(int fd,off_t at,const void *data,size_t n){assert(pwrite(fd,data,n,at)==(ssize_t)n);}
int main(void){
 assert(!navigation_profile_hash("unknown"));assert(!navigation_profile_process(-1));
 char name[]="/tmp/zigma-nav-test-XXXXXX";int fd=mkstemp(name);assert(fd>=0);unlink(name);
 for(size_t j=0;j<sizeof navigation_profiles/sizeof *navigation_profiles;j++){
  assert(!ftruncate(fd,0));const struct navigation_profile *p=&navigation_profiles[j];
  assert(navigation_profile_hash(p->sha256)==p);
  write_at(fd,p->slot,&p->base,4);write_at(fd,p->base,&p->vtable,4);
  float values[360],got[360];for(int i=0;i<360;i++)values[i]=(float)i/100;
  write_at(fd,p->base+4,values,sizeof values);assert(navigation_profile_fd(fd,p)==p);assert(laser_copy(fd,got,p)&&!memcmp(values,got,sizeof values));
  values[30]=NAN;write_at(fd,p->base+4,values,sizeof values);assert(!laser_copy(fd,got,p));values[30]=-1;write_at(fd,p->base+4,values,sizeof values);assert(!laser_copy(fd,got,p));values[30]=101;write_at(fd,p->base+4,values,sizeof values);assert(!laser_copy(fd,got,p));
  unsigned char pose[40]={1,1};float xyh[]={1,1,0.5},orig[]={10,10},res=0.05;uint32_t dims[]={100,100};memcpy(pose+28,xyh,12);
  write_at(fd,p->base+p->pose_offset,pose,sizeof pose);write_at(fd,p->base+0xe4c,orig,8);write_at(fd,p->base+0xe54,dims,8);write_at(fd,p->base+0xe5c,&res,4);
  cJSON *g=cJSON_Parse("{\"map_id\":\"21\",\"width\":100,\"height\":100,\"map_origin\":[10,89]}");assert(g);cJSON *o=cached_navigation_pose(fd,g,p);assert(o&&fabs(get(o,"x")->valuedouble-30)<0.01&&fabs(get(o,"y")->valuedouble-69)<0.01);cJSON_Delete(o);
  assert(!cached_navigation_pose(fd,g,NULL));
  for(size_t k=0;k<sizeof navigation_profiles/sizeof *navigation_profiles;k++)if(k!=j){
   assert(!cached_navigation_pose(fd,g,&navigation_profiles[k]));assert(!laser_copy(fd,got,&navigation_profiles[k]));
  }
  if(!strcmp(p->name,"zigma-issue04")){
   /* A valid old-offset pose must not pass the Russian reader. */
   unsigned char zero[64]={0};write_at(fd,p->base+0x1120,zero,sizeof zero);write_at(fd,p->base+0x1120,pose,sizeof pose);
   assert(!cached_navigation_pose(fd,g,p));write_at(fd,p->base+p->pose_offset,pose,sizeof pose);
   o=cached_navigation_pose(fd,g,p);assert(o&&fabs(get(o,"heading")->valuedouble+0.5)<0.001);cJSON_Delete(o);
  }
  pose[0]=0;write_at(fd,p->base+p->pose_offset,pose,40);assert(!cached_navigation_pose(fd,g,p));cJSON_Delete(g);
  uint32_t bad=0;write_at(fd,p->base,&bad,4);assert(!navigation_profile_fd(fd,p)&&!laser_copy(fd,got,p));
 }
 assert(!ftruncate(fd,0));assert(!navigation_profile_fd(fd,NULL));close(fd);puts("Zigma/Puron/Issue04 scan layouts, invalid ranges, pose geometry and identity guards: PASS");return 0;
}
