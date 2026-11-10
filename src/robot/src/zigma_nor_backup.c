#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "zigma_version.h"
#include "mbedtls/sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/ioctl.h>
#include <mtd/mtd-user.h>
/* Protocol: JSON header LF, two consecutive full NOR reads, JSON result LF.
 * Only the consumer retains the first copy. No flash writes or snapshot claim. */
#define TOTAL (32u*1024u*1024u)
#define BLOCK 65536u
static unsigned char buf[BLOCK];
static void fail(const char *s){fprintf(stderr,"NOR backup: %s\n",s);exit(1);}
static void hex(const unsigned char *p,char *s){for(int i=0;i<32;i++)sprintf(s+2*i,"%02x",p[i]);}
static void output(const void *p,size_t n){if(fwrite(p,1,n,stdout)!=n)fail("stream disconnected");}
int zigma_nor_main(int argc,char **argv){
 if(zigma_version_only(argc,argv,"zigma-nor-backup-arm"))return 0;
 if(argc!=2||strcmp(argv[1],"--stream"))fail("use --stream");
 alarm(600);signal(SIGPIPE,SIG_DFL);
 int lock=open("/tmp/zigma-nor-backup.lock",O_CREAT|O_RDWR|O_NOFOLLOW|O_CLOEXEC,0600);
 if(lock<0||flock(lock,LOCK_EX|LOCK_NB))fail("another backup is running");
 unsigned sizes[6]={0},erase[6]={0},index,count=0;char names[6][32],line[256];int fds[6];
#ifdef NOR_TEST
 const char *root=getenv("NOR_TEST_ROOT");if(!root)fail("test root missing");char filename[512];snprintf(filename,sizeof filename,"%s/proc-mtd",root);FILE *f=fopen(filename,"r");
#else
 FILE *f=fopen("/proc/mtd","r");
#endif
 if(!f)fail("cannot read partition table");
 if(!fgets(line,sizeof line,f))fail("missing partition table");
 while(fgets(line,sizeof line,f)){
  unsigned size,er;char name[32],extra;
  if(count==6||sscanf(line,"mtd%u: %x %x \"%31[^\"]\" %c",&index,&size,&er,name,&extra)!=4||index!=count)fail("unsupported partition table");
  sizes[count]=size;erase[count]=er;strcpy(names[count],name);count++;
 }fclose(f);
 const unsigned direct[]={0x100000,0x10000,0x300000,0x600000,0x15f0000};
 const unsigned overlay[]={0x50000,0x10000,0x300000,0x800000,0x4a0000,0x1000000};
 const char *expected[]={"uboot","dtb","kernel","rootfs","data","firmware"};
 if(!((count==5&&!memcmp(sizes,direct,sizeof direct))||(count==6&&!memcmp(sizes,overlay,sizeof overlay))))fail("unreviewed NOR geometry; SSH remains available");
 for(unsigned i=0;i<count;i++){
  if(strcmp(names[i],expected[i])||erase[i]!=BLOCK)fail("unreviewed partition name/erase size");
  char p[512];
#ifdef NOR_TEST
  snprintf(p,sizeof p,"%s/mtd%u",root,i);
#else
  snprintf(p,sizeof p,"/dev/mtd%u",i);
#endif
  fds[i]=open(p,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fds[i]<0)fail("cannot open NOR read-only");
#ifndef NOR_TEST
  struct stat st;struct mtd_info_user info;
  if(fstat(fds[i],&st)||!S_ISCHR(st.st_mode)||major(st.st_rdev)!=90||minor(st.st_rdev)!=2*i||ioctl(fds[i],MEMGETINFO,&info)||info.type!=MTD_NORFLASH||info.size!=sizes[i]||info.erasesize!=erase[i])fail("MTD device mismatch");
#endif
 }
 printf("{\"format\":\"zigma-nor-v1\",\"size\":%u,\"block_size\":%u,\"passes\":2,\"partitions\":[",TOTAL,BLOCK);
 unsigned offset=0;
 for(unsigned i=0;i<count;i++){printf("%s{\"index\":%u,\"name\":\"%s\",\"offset\":%u,\"size\":%u}",i?",":"",i,names[i],offset,sizes[i]);offset+=sizes[i];}
 printf("]}\n");fflush(stdout);
 char hashes[2][65];
 for(unsigned pass=0;pass<2;pass++){
  mbedtls_sha256_context sha;mbedtls_sha256_init(&sha);if(mbedtls_sha256_starts(&sha,0))fail("sha init");
  for(unsigned i=0;i<count;i++)for(unsigned pos=0;pos<sizes[i];pos+=BLOCK){
   size_t done=0;while(done<BLOCK){ssize_t n=pread(fds[i],buf+done,BLOCK-done,pos+done);if(n<=0)fail("short NOR read");done+=(size_t)n;}
   if(mbedtls_sha256_update(&sha,buf,BLOCK))fail("sha update");
   output(buf,BLOCK);
  }
  unsigned char digest[32];if(mbedtls_sha256_finish(&sha,digest))fail("sha finish");mbedtls_sha256_free(&sha);hex(digest,hashes[pass]);
 }
 int equal=!strcmp(hashes[0],hashes[1]);printf("{\"verified\":%s,\"first_sha256\":\"%s\",\"second_sha256\":\"%s\"}\n",equal?"true":"false",hashes[0],hashes[1]);
 if(fflush(stdout))fail("stream disconnected");
 for(unsigned i=0;i<count;i++)close(fds[i]);
 close(lock);return equal?0:2;
}

#ifndef NOR_EMBED
int main(int argc,char **argv){return zigma_nor_main(argc,argv);}
#endif
