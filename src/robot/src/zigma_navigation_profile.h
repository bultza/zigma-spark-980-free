/* Passive CommVSLAM layouts, selected by exact running executable SHA256.
 * Issue04: RTTI/constructor 0x74498, getter 0x5f1d0 and writer 0x7d354
 * independently establish a +12-byte shift of the pose block, not the scan
 * or geometry. Statically traced; Russian hardware runtime validation pending. */
struct navigation_profile {const char *name,*sha256;uint32_t slot,base,vtable,pose_offset;};
static const struct navigation_profile navigation_profiles[]={
 {"zigma","b10fc7b48d1bdb990a2c37b82f7d45fa4fd7358f428f42d58ae885aca2970255",0x444594,0x45a200,0x43b4f4,0x1120},
 {"puron","6b7b11091132e75d9787abd59a8ac4f758da3732cccf86dc5702e99bd772a5d8",0x3a5e30,0x3b6020,0x39d400,0x1120},
 {"zigma-issue04","1b2751efd2a4d5afc208fce099401d90c581f0cec41cfe440e9a39d519ece664",0x4687a4,0x47e440,0x45f544,0x112c},
 /* Recognized map-only build: no validated passive RAM contract. */
 {"robojet-issue08-map-only","f793e93abb85a1a7b1cbe8cec1f53b2c6d9aac7c21c8fb3b956f460fe693f88c",0,0,0,0}
};
static inline const struct navigation_profile *navigation_profile_hash(const char *hash){
 for(size_t i=0;i<sizeof(navigation_profiles)/sizeof(*navigation_profiles);i++)if(!strcmp(hash,navigation_profiles[i].sha256))return &navigation_profiles[i];
 return NULL;
}
/* Hash the running executable, not its pathname on disk. Revalidate identity
 * each call; avoid rehashing while PID/inode/size/timestamps stay unchanged. */
static inline const struct navigation_profile *navigation_profile_process(int pid){
 static int cached_pid; static struct stat cached; static const struct navigation_profile *profile;
 char path[64];struct stat st,after;snprintf(path,sizeof path,"/proc/%d/exe",pid);
 int fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0)return NULL;
 if(fstat(fd,&st)){close(fd);return NULL;}
 if(cached_pid==pid&&cached.st_dev==st.st_dev&&cached.st_ino==st.st_ino&&cached.st_size==st.st_size&&cached.st_mtim.tv_sec==st.st_mtim.tv_sec&&cached.st_mtim.tv_nsec==st.st_mtim.tv_nsec&&cached.st_ctim.tv_sec==st.st_ctim.tv_sec&&cached.st_ctim.tv_nsec==st.st_ctim.tv_nsec){close(fd);return profile;}
 mbedtls_sha256_context ctx;mbedtls_sha256_init(&ctx);int rc=mbedtls_sha256_starts(&ctx,0);
 unsigned char bytes[16384],digest[32];ssize_t n=0;
 while(!rc&&(n=read(fd,bytes,sizeof bytes))>0)rc=mbedtls_sha256_update(&ctx,bytes,(size_t)n);
 if(n<0||fstat(fd,&after)||st.st_size!=after.st_size||st.st_mtim.tv_sec!=after.st_mtim.tv_sec||st.st_mtim.tv_nsec!=after.st_mtim.tv_nsec||st.st_ctim.tv_sec!=after.st_ctim.tv_sec||st.st_ctim.tv_nsec!=after.st_ctim.tv_nsec)rc=1;
 if(!rc)rc=mbedtls_sha256_finish(&ctx,digest);
 mbedtls_sha256_free(&ctx);close(fd);if(rc)return NULL;
 char hash[65];for(unsigned i=0;i<32;i++)snprintf(hash+2*i,3,"%02x",digest[i]);
 cached_pid=pid;cached=st;profile=navigation_profile_hash(hash);return profile;
}

static const struct navigation_profile *navigation_profile_fd(int fd,const struct navigation_profile *p){
 uint32_t base=0,again=0,vt=0;
 if(p&&p->slot&&p->base&&p->vtable&&p->pose_offset&&memory_read(fd,p->slot,&base,4)&&base==p->base&&memory_read(fd,base,&vt,4)&&vt==p->vtable&&memory_read(fd,p->slot,&again,4)&&again==base)return p;
 return NULL;
}
