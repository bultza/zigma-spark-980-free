/* Passive CommVSLAM layouts, independently traced for each native build.
 * Puron: singleton 0x27a1c, constructor 0x6a2e0, scan copier 0x762ec,
 * exporter 0x5eec8 (metres / 0.001). Geometry fields: 0x64eda..0x64f20.
 * Zigma's existing addresses and interpretation are unchanged. */
struct navigation_profile {const char *name;uint32_t slot,base,vtable;};
static const struct navigation_profile navigation_profiles[]={
 {"zigma",0x444594,0x45a200,0x43b4f4},
 {"puron",0x3a5e30,0x3b6020,0x39d400}
};
static const struct navigation_profile *navigation_profile_fd(int fd){
 for(size_t i=0;i<sizeof navigation_profiles/sizeof *navigation_profiles;i++){
  const struct navigation_profile *p=&navigation_profiles[i];uint32_t base=0,again=0,vt=0;
  if(memory_read(fd,p->slot,&base,4)&&base==p->base&&memory_read(fd,base,&vt,4)&&vt==p->vtable&&memory_read(fd,p->slot,&again,4)&&again==base)return p;
 }return NULL;
}
