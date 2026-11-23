/* Exact native receiver identity for local geometry commands. */
#include "zigma_native_identity.h"
#include "mbedtls/sha256.h"
#include <sys/msg.h>
static inline bool zones_process_hash(int pid,const char *expected,char hash[65]){
 if(!zigma_native_process(pid,expected))return false;
 char path[64];snprintf(path,sizeof path,"/proc/%d/exe",pid);
 FILE *f=fopen(path,"rb");if(!f)return false;
 mbedtls_sha256_context ctx;mbedtls_sha256_init(&ctx);int rc=mbedtls_sha256_starts(&ctx,0);
 unsigned char bytes[8192],digest[32];size_t n;
 while(!rc&&(n=fread(bytes,1,sizeof bytes,f)))rc=mbedtls_sha256_update(&ctx,bytes,n);
 if(ferror(f))rc=1;
 if(!rc)rc=mbedtls_sha256_finish(&ctx,digest);
 fclose(f);mbedtls_sha256_free(&ctx);if(rc)return false;
 for(int i=0;i<32;i++)snprintf(hash+2*i,3,"%02x",digest[i]);
 return true;
}

static inline bool zones_receiver_hash(int q,const char *expected,char hash[65]){
 struct msqid_ds info;return !msgctl(q,IPC_STAT,&info)&&zones_process_hash(info.msg_lrpid,expected,hash);
}
