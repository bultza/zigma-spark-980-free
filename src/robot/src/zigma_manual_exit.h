/* Caccamora-only native queue transaction, integrated into zigmactl.
 * Proven event sequence: release14/a1 then pause1b/0. No raw SPI injection.
 * The child is a transient part of zigmactl, not a separate installed program.
 */
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <poll.h>
#define Q 0x4a9004u
#define FIRST 0x4a9130u
#define END (FIRST+50u)
struct task { pid_t id; int stopped; int resume_signal; } mq_tasks[128];
static size_t mq_nt;
static int mq_mem=-1;
static int mq_release_request=0;
static long long mq_now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (long long)t.tv_sec*1000+t.tv_nsec/1000000;}
static inline int mq_read_at(int fd,uint32_t a,void *b,size_t n){return pread(fd,b,n,a)==(ssize_t)n?0:-1;}
static inline int mq_write_at(int fd,uint32_t a,const void *b,size_t n){return pwrite(fd,b,n,a)==(ssize_t)n?0:-1;}
static uint32_t mq_le32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static inline void mq_put32(unsigned char *p,uint32_t v){for(int i=0;i<4;i++)p[i]=(unsigned char)(v>>(i*8));}
static inline int mq_plan(const unsigned char q[20],const unsigned char joy[12],uint32_t state,uint32_t timer,uint32_t *slot,uint32_t *next){
 uint32_t w=mq_le32(q+8),r=mq_le32(q+12);
 if(state!=7||timer||joy[8]!=1||joy[9]<0x9d||joy[9]>0xa1)return -1;
 if(!mq_release_request)for(int i=0;i<8;i++)if(joy[i])return -1;
 if(mq_le32(q)!=FIRST||mq_le32(q+4)!=END||q[16]!=10||q[17]||q[18]!=5||w!=r||w<FIRST||w>=END||(w-FIRST)%5)return -1;
 *slot=w;*next=w+5==END?FIRST:w+5;return 0;
}
static inline int mq_text_file(const char *path,char *out,size_t n){int f=open(path,O_RDONLY|O_CLOEXEC);if(f<0)return -1;ssize_t k=read(f,out,n-1);close(f);if(k<=0)return -1;out[k]=0;out[strcspn(out,"\r\n")]=0;return 0;}
static inline void mq_release(void){for(size_t i=mq_nt;i>0;i--)if(mq_tasks[i-1].stopped)ptrace(PTRACE_DETACH,mq_tasks[i-1].id,0,mq_tasks[i-1].resume_signal);if(mq_mem>=0)close(mq_mem);}
static inline int mq_freeze(pid_t pid){
 char path[80];snprintf(path,sizeof(path),"/proc/%d/task",pid);long long deadline=mq_now_ms()+300;
 for(int pass=0;pass<4;pass++){
  DIR *d=opendir(path);if(!d)return -1;struct dirent *e;int added=0;
  while((e=readdir(d))){char *end;long id=strtol(e->d_name,&end,10);if(*end||id<=0)continue;size_t i;for(i=0;i<mq_nt;i++)if(mq_tasks[i].id==id)break;if(i<mq_nt)continue;
   if(mq_nt==128){closedir(d);return -1;}
   char stpath[96],stbuf[2048];snprintf(stpath,sizeof(stpath),"/proc/%d/task/%ld/stat",pid,id);
   if(mq_text_file(stpath,stbuf,sizeof(stbuf))){closedir(d);return -1;}char *sp=strrchr(stbuf,')');if(!sp||sp[2]=='T'||sp[2]=='t'){closedir(d);return -1;}
   if(ptrace(PTRACE_SEIZE,id,0,0)){closedir(d);return -1;}
   mq_tasks[mq_nt++]=(struct task){(pid_t)id,0,0};added++;
   if(ptrace(PTRACE_INTERRUPT,id,0,0)){closedir(d);return -1;}
  }closedir(d);
  for(size_t i=0;i<mq_nt;i++)if(!mq_tasks[i].stopped){int status;pid_t r;
   do {r=waitpid(mq_tasks[i].id,&status,__WALL|WNOHANG);if(r==0){struct timespec delay={0,1000000};nanosleep(&delay,NULL);}}while(r==0&&mq_now_ms()<deadline);
   /* Only our interrupt stop; preserve external signal/group stops by refusing. */
   if(r!=mq_tasks[i].id||!WIFSTOPPED(status))return -1;
   mq_tasks[i].stopped=1;
   if((status>>16)!=PTRACE_EVENT_STOP||WSTOPSIG(status)!=SIGTRAP){mq_tasks[i].resume_signal=WSTOPSIG(status);return -1;}
  }
  if(!added)return 0;
  if(mq_now_ms()>=deadline)return -1;
 }return -1;
}
static inline int mq_in_queue(uint32_t pc){pc&=~1u;return pc>=0x1bc580&&pc<0x1bc800;}
static inline int mq_quiescent(void){
#ifdef __arm__
 for(size_t i=0;i<mq_nt;i++){uint32_t regs[18];if(ptrace(PTRACE_GETREGS,mq_tasks[i].id,0,regs))return -1;if(mq_in_queue(regs[15])||mq_in_queue(regs[14]))return -1;}
 return 0;
#else
 (void)mq_in_queue;return -1;
#endif
}
static inline int mq_publish(int fd,uint32_t timer_value){
 unsigned char q[20],j[12],state[4];uint32_t slot,next;
 if(mq_read_at(fd,Q,q,20)||mq_read_at(fd,0x4a9668,j,12)||mq_read_at(fd,0x4a8c48,state,4)||mq_plan(q,j,mq_le32(state),timer_value,&slot,&next))return 4;
 unsigned char original[5],event[5]={mq_release_request?0x14:0x1b,mq_release_request?0xa1:0,0,0,0},check[5],ptr[4];if(mq_read_at(fd,slot,original,5))return 4;
 if(mq_write_at(fd,slot,event,5)||mq_read_at(fd,slot,check,5)||memcmp(check,event,5)){if(mq_write_at(fd,slot,original,5))return 6;return 4;}
 mq_put32(ptr,next);
 /* Pointer publication is the commit. Never retry or roll back after this. */
 if(mq_write_at(fd,Q+8,ptr,4)||mq_read_at(fd,Q+8,check,4)||memcmp(check,ptr,4))return 5;
 return 0;
}

#undef Q
#undef FIRST
#undef END
struct manual_sample {
    uint32_t state, timer, app_state, app_error;
    bool neutral, empty;
    unsigned active,last;
};
static int manual_memory(int pid) {
    char path[64];snprintf(path,sizeof path,"/proc/%d/mem",pid);
    return open(path,O_RDONLY|O_CLOEXEC);
}
static bool manual_sample_native(struct manual_sample *s) {
    if(!manual_native_alive())return false;
    int n=manual_memory(manual_nav),a=manual_memory(manual_app);
    if(n<0||a<0){if(n>=0)close(n);if(a>=0)close(a);return false;}
    unsigned char q[20],j[12],state[4],timer[4],status[100],again[100],init[4];
    bool ok=!mq_read_at(n,0x4a9004,q,20)&&!mq_read_at(n,0x4a9668,j,12)&&
        !mq_read_at(n,0x4a8c48,state,4)&&!mq_read_at(a,0x48c08,timer,4)&&
        !mq_read_at(a,0x49d30,init,4)&&mq_le32(init)==1&&
        !mq_read_at(a,0x48e24,status,100)&&!mq_read_at(a,0x48e24,again,100)&&!memcmp(status,again,100);
    close(n);close(a);
    if(!ok||!manual_native_alive())return false;
    uint32_t w=mq_le32(q+8),r=mq_le32(q+12);
    if(mq_le32(q)!=0x4a9130||mq_le32(q+4)!=0x4a9162||q[16]!=10||q[17]||q[18]!=5||
       w<0x4a9130||w>=0x4a9162||r<0x4a9130||r>=0x4a9162||(w-0x4a9130)%5||(r-0x4a9130)%5)return false;
    *s=(struct manual_sample){.state=mq_le32(state),.timer=mq_le32(timer),
        .app_state=mq_le32(status),.app_error=mq_le32(status+44),.empty=w==r,.neutral=true,.active=j[8],.last=j[9]};
    for(int i=0;i<8;i++)if(j[i])s->neutral=false;
    return true;
}
static int manual_event_native(bool releasing,double deadline) {
#ifndef __arm__
    (void)releasing;(void)deadline;return 4;
#else
    if(!manual_native_alive())return 4;
    pid_t child=fork();if(child<0)return 4;
    if(!child){
        /* Never inherit the session's cancellation handler into the tracer.
         * Parent timeout uses SIGKILL; kernel then detaches every seized task. */
        signal(SIGINT,SIG_IGN);signal(SIGTERM,SIG_IGN);signal(SIGHUP,SIG_IGN);
        mq_nt=0;mq_mem=-1;mq_release_request=releasing;
        int result=4;
        if(!manual_native_alive()||mq_freeze(manual_nav)||mq_quiescent())goto child_done;
        if(!manual_native_alive())goto child_done;
        char path[64];snprintf(path,sizeof path,"/proc/%d/mem",manual_nav);
        mq_mem=open(path,O_RDWR|O_CLOEXEC);if(mq_mem<0)goto child_done;
        int a=manual_memory(manual_app);unsigned char timer[4];if(a<0)goto child_done;
        int err=mq_read_at(a,0x48c08,timer,4);close(a);if(err)goto child_done;
        result=mq_publish(mq_mem,mq_le32(timer));
child_done:
        mq_release();_exit(result);
    }
    double until=nav_clock()+0.7;if(until>deadline)until=deadline;
    int status;
    do {
        pid_t r=waitpid(child,&status,WNOHANG);
        if(r==child)return WIFEXITED(status)?WEXITSTATUS(status):5;
        if(r<0&&errno!=EINTR)break;
        struct timespec delay={0,5000000};nanosleep(&delay,NULL);
    }while(nav_clock()<until);
    kill(child,SIGKILL);while(waitpid(child,&status,0)<0&&errno==EINTR){}
    /* Publication may have happened before timeout: never replay it. */
    return 5;
#endif
}
