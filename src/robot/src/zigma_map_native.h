#include "zigma_app_profile.h"
/* Exact get_map_id fingerprints include the cache-base literal. Puron
 * 784fbcda: get_map_id 0x21d40 -> 0x48f6c + 0x48; recv_packet uses
 * 96 bytes at 0x48980 and system_info_init at 0x49870. */
struct map_native_profile {const char *name;uint32_t getter,base,initialized,status;size_t status_size;bool restore_supported,clear_supported;};
static const struct map_native_profile map_profiles[]={
 {"zigma",0x22714,0x4941c,0x49d30,0x48e24,100,true,true},
 {"puron",0x21d40,0x48f6c,0x49870,0x48980,96,false,true},
 {"zigma-issue04",0x22ea0,0x4a248,0x4ab5c,0x49c44,100,true,true}
};
static const struct map_native_profile *map_profile_fd(int fd){
 const unsigned char prefix[]={1,0x4b,0x98,0x6c,0x70,0x47,0,0xbf};
 for(size_t i=0;i<sizeof(map_profiles)/sizeof(*map_profiles);i++){
  unsigned char code[12];const struct map_native_profile *p=&map_profiles[i];
  if(pread(fd,code,sizeof code,p->getter)==sizeof code&&!memcmp(code,prefix,sizeof prefix)&&u32(code+8)==p->base)return p;
 }return NULL;
}
static int native_map_fd(const char *pidtext){
 uint32_t pid;if(!map_id(pidtext,&pid)||pid<2||!zigma_native_process((int)pid,"/root/app_server/wifi_app_server_grit"))return -1;
 const struct zigma_app_profile *profile=zigma_app_profile_process((int)pid);if(!profile)return -1;
 char path[80];snprintf(path,sizeof path,"/proc/%u/mem",pid);int fd=open(path,O_RDONLY|O_CLOEXEC);
 if(fd>=0){const struct map_native_profile *mp=map_profile_fd(fd);if(!mp||strcmp(mp->name,profile->name)){close(fd);return -1;}}return fd;
}
static bool map_initialized(int fd,const struct map_native_profile *p){unsigned char b[4];return p&&pread(fd,b,4,p->initialized)==4&&u32(b)==1;}
static bool map_identity_fd(int fd,uint32_t *id){
 const struct map_native_profile *p=map_profile_fd(fd);unsigned char a[4],b[4];
 if(!map_initialized(fd,p)||pread(fd,a,4,p->base+0x48)!=4||pread(fd,b,4,p->base+0x48)!=4||memcmp(a,b,4)||!map_initialized(fd,p)||u32(a)>INT32_MAX)return false;
 *id=u32(a);return true;
}
static bool map_idle_fd(int fd){
 const struct map_native_profile *p=map_profile_fd(fd);unsigned char a[100],b[100];
 if(!map_initialized(fd,p)||pread(fd,a,p->status_size,p->status)!=(ssize_t)p->status_size||pread(fd,b,p->status_size,p->status)!=(ssize_t)p->status_size||memcmp(a,b,p->status_size)||!map_initialized(fd,p))return false;
 uint32_t work=u32(a);return (work==1||work==2||work==4||work==30||work==31)&&u32(a+44)==0;
}
/* Navigation owns the current ID. Puron's app cache retains the old ID after
 * ClearMap. Exact 6b7b1109 getter/GOT/initialized singleton fingerprints. */
static bool puron_navigation_identity_fd(int fd,uint32_t *id){
 const unsigned char getter[]={0x68,0x6c,2,0xb0,0x70,0xbd};
 const unsigned char singleton[]={0x70,0xb5,0x14,0x4c,0x14,0x4b,0x7c,0x44,0xe5,0x58,0x2b,0x78,0xbf,0xf3,0x5b,0x8f,0xdb,7,3,0xd5,0x11,0x4b,0xe6,0x58,0x30,0x46};
 unsigned char code[26],a[4],b[4],guard;
 if(pread(fd,code,sizeof getter,0x13fba6)!=sizeof getter||memcmp(code,getter,sizeof getter)||pread(fd,code,sizeof singleton,0x5c474)!=sizeof singleton||memcmp(code,singleton,sizeof singleton))return false;
 if(pread(fd,a,4,0x3a5a24)!=4||u32(a)!=0x3c26d0||pread(fd,a,4,0x3a5d80)!=4||u32(a)!=0x3c26d4)return false;
 if(pread(fd,&guard,1,0x3c26d0)!=1||guard!=1||pread(fd,a,4,0x3c2718)!=4||pread(fd,b,4,0x3c2718)!=4||memcmp(a,b,4)||u32(a)>INT32_MAX||pread(fd,&guard,1,0x3c26d0)!=1||guard!=1)return false;
 *id=u32(a);return true;
}
static bool puron_navigation_identity(uint32_t *id){
 key_t key=ftok("/",14);int q=key==(key_t)-1?-1:msgget(key,0);struct msqid_ds info;
 if(q<0||msgctl(q,IPC_STAT,&info)||!zigma_native_process(info.msg_lrpid,"/root/hoslam/demo_rplidar"))return false;
 char path[80];snprintf(path,sizeof path,"/proc/%d/mem",info.msg_lrpid);int fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0)return false;
 bool ok=puron_navigation_identity_fd(fd,id);close(fd);return ok;
}
static bool native_identity(const char *pid,uint32_t *id){int fd=native_map_fd(pid);if(fd<0)return false;const struct map_native_profile *p=map_profile_fd(fd);bool puron=p&&!strcmp(p->name,"puron");bool ok=map_identity_fd(fd,id);close(fd);return ok&&(puron?puron_navigation_identity(id):true);}
static bool native_active(const char *pid,uint32_t *id){return native_identity(pid,id)&&*id>0;}
static bool native_idle(const char *pid){int fd=native_map_fd(pid);if(fd<0)return false;bool ok=map_idle_fd(fd);close(fd);return ok;}
static bool native_restore_supported(const char *pid){int fd=native_map_fd(pid);if(fd<0)return false;const struct map_native_profile *p=map_profile_fd(fd);bool ok=p&&p->restore_supported;close(fd);return ok;}
static bool native_clear_supported(const char *pid){int fd=native_map_fd(pid);if(fd<0)return false;const struct map_native_profile *p=map_profile_fd(fd);bool ok=p&&p->clear_supported;close(fd);return ok;}
static int native_active_command(const char *pid){
 uint32_t id;if(!native_identity(pid,&id))return lib_error("Native active-map cache unavailable");
 cJSON *o=cJSON_CreateObject();char text[16];snprintf(text,sizeof text,"%u",id);cJSON_AddStringToObject(o,"id",text);
 cJSON_AddStringToObject(o,"source","native_current_map_identity");cJSON_AddNumberToObject(o,"cache_read_at",time(NULL));
 cJSON_AddBoolToObject(o,"clear_available",id>0&&native_clear_supported(pid));return print_json(o);
}
