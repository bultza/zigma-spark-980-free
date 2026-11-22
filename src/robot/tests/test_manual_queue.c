#define _POSIX_C_SOURCE 200809L
#define main diagnostic_main
#include "../src/zigmactl.c"
#undef main
#include <pthread.h>
#include <assert.h>
#include <sys/mman.h>
#define Q 0x4a9004u
#define FIRST 0x4a9130u
#define END (FIRST+50u)
static void *sleeper(void *unused){(void)unused;for(;;)pause();return NULL;}
static void planner_test(void){
 unsigned char q[20]={0},j[12]={0};uint32_t slot,next;
 mq_put32(q,FIRST);mq_put32(q+4,END);q[16]=10;q[18]=5;j[8]=1;j[9]=0x9d;
 for(int release=0;release<2;release++){
  mq_release_request=release;
  for(int i=0;i<10;i++){
   mq_put32(q+8,FIRST+5*i);mq_put32(q+12,FIRST+5*i);
   assert(!mq_plan(q,j,7,0,&slot,&next));assert(slot==FIRST+5*(unsigned)i);
   assert(next==(i==9?FIRST:slot+5));
   assert(mq_plan(q,j,26,0,&slot,&next));assert(mq_plan(q,j,7,1,&slot,&next));
  }
  j[8]=0;assert(mq_plan(q,j,7,0,&slot,&next));j[8]=1;
  j[0]=1;assert((mq_plan(q,j,7,0,&slot,&next)==0)==(release!=0));j[0]=0;
  mq_put32(q+12,FIRST);assert(mq_plan(q,j,7,0,&slot,&next));
 }
 puts("PASS: queue planner positions, wrap and refusal guards");
}
int main(int argc,char **argv){
 planner_test();

 mq_release_request=argc==2&&!strcmp(argv[1],"release");
 int ready[2],go[2];if(pipe(ready)||pipe(go))return 2;pid_t child=fork();if(child<0)return 2;
 if(!child){close(ready[0]);close(go[1]);
 unsigned char *m=mmap((void*)0x4a8000,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);if(m==MAP_FAILED)_exit(2);
 unsigned char *q=(void*)Q;mq_put32(q,FIRST);mq_put32(q+4,END);mq_put32(q+8,END-5);mq_put32(q+12,END-5);q[16]=10;q[18]=5;mq_put32((void*)0x4a8c48,7);unsigned char *j=(void*)0x4a9668;j[8]=1;j[9]=0x9d;if(mq_release_request){j[0]=200;j[2]=200;j[4]=200;j[6]=200;}
 pthread_t t;if(pthread_create(&t,0,sleeper,0))_exit(2);char c='R';if(write(ready[1],&c,1)!=1||read(go[0],&c,1)!=1)_exit(2);if(mq_le32(q+8)!=FIRST||memcmp((void*)(END-5),mq_release_request?"\x14\xa1\0\0\0":"\x1b\0\0\0\0",5))_exit(4);_exit(0);}
 close(ready[1]);close(go[0]);char c;if(read(ready[0],&c,1)!=1)return 2;
 int rc=mq_freeze(child);size_t frozen=mq_nt;
 if(!rc){char path[80];snprintf(path,sizeof(path),"/proc/%d/mem",child);mq_mem=open(path,O_RDWR);if(mq_mem<0||mq_publish(mq_mem,0))rc=-1;}
 mq_release();mq_mem=-1;mq_nt=0;
 if(rc){fprintf(stderr,"ptrace fixture unavailable/failed: %s\n",strerror(errno));kill(child,SIGKILL);waitpid(child,0,0);return 3;}
 c='G';if(write(go[1],&c,1)!=1)return 2;int status;waitpid(child,&status,0);
 if(frozen!=2||!WIFEXITED(status)||WEXITSTATUS(status))return 4;
 puts("PASS: two-thread fixture quiesced, one event published with wrap, resumed and observed by child");return 0;
}
