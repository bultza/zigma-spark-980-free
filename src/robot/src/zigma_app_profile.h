#ifndef ZIGMA_APP_PROFILE_H
#define ZIGMA_APP_PROFILE_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "mbedtls/sha256.h"
struct zigma_app_profile { const char *name,*sha256; uint32_t status,initialized,volume,zones; size_t status_size; };
static const struct zigma_app_profile zigma_app_profiles[]={
 {"zigma","4cf0709919f0e894a5d3ed1c60ca9d6bf11d7c47120077e23f6c13902c5d9a9a",0x48e24,0x49d30,0x48f66,0x49d5c,100},
 {"puron","784fbcda505db72675bf01a82f5b02f6eca8cb313a7916176ed7bbf10d7dd912",0x48980,0x49870,0x48ab6,0x4989c,96},
 /* Issue04 recv_packet: 100-byte state; classified zones at 0x4ab60+0x28,
  * allocated/copied as 4 + 36*count. Original Ciccia navigation is separate. */
 {"zigma-issue04","8084000b686d071f8f0590448087ff88d71deee3e55e772c86d41f27d704516c",0x49c44,0x4ab5c,0x49d92,0x4ab88,100}
};
static inline const struct zigma_app_profile *zigma_app_profile_hash(const char *hash){
 for(size_t i=0;i<sizeof(zigma_app_profiles)/sizeof(*zigma_app_profiles);i++)if(!strcmp(hash,zigma_app_profiles[i].sha256))return &zigma_app_profiles[i];
 return NULL;
}
/* Hash the running executable, not its pathname on disk. Revalidate identity
 * each call; avoid rehashing while PID/inode/size/timestamps stay unchanged. */
static inline const struct zigma_app_profile *zigma_app_profile_process(int pid){
 static int cached_pid; static struct stat cached; static const struct zigma_app_profile *profile;
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
 cached_pid=pid;cached=st;profile=zigma_app_profile_hash(hash);return profile;
}
#endif
