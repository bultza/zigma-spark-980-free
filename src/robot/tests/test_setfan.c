#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#define main control_main
#define msgget fake_msgget
#define msgsnd fake_msgsnd
#define nanosleep fake_nanosleep
#include "../src/zigmactl.c"
#undef main
static int sends,fail_send,fail_queue,waits;
static long waited;
int fake_nanosleep(const struct timespec *req,struct timespec *rem){assert(sends==2);assert(req->tv_sec==0);long n=req->tv_nsec;waits++;if(waits==1){assert(n==500000000L);waited+=200000000L;rem->tv_sec=0;rem->tv_nsec=300000000L;errno=EINTR;return -1;}waited+=n;return 0;}
static unsigned char frames[3][6];
int fake_msgget(key_t k,int f){(void)k;assert(f==0);if(fail_queue){errno=ENOENT;return -1;}return 9;}
int fake_msgsnd(int q,const void *p,size_t n,int f){const struct zigma_msg *m=p;assert(q==9&&n==4096&&f==IPC_NOWAIT&&m->mtype==1);assert(m->data[0]==6&&m->data[1]==0&&m->data[2]==0&&m->data[3]==0);assert(sends<3);memcpy(frames[sends++],m->data+4,6);if(fail_send&&sends==fail_send){errno=EAGAIN;return -1;}return 0;}
int main(void){
 const unsigned char expected[3][6]={{0xaa,3,2,0x24,2,0x28},{0xaa,3,2,0x24,0,0x26},{0xaa,3,2,0x24,1,0x27}};
 char *a[]={"zigmactl","setfan","1",NULL,NULL};
 for(int i=0;i<3;i++){char level[2]={(char)('1'+i),0};a[2]=level;sends=0;assert(control_main(3,a)==0&&sends==1);assert(!memcmp(frames[0],expected[i],6));}
 char *invalid[]={"0","4","-1","","01","1x","1.0"," 1"};
 for(unsigned i=0;i<sizeof invalid/sizeof *invalid;i++){a[2]=invalid[i];sends=0;assert(control_main(3,a)!=0&&sends==0);}
 sends=0;assert(control_main(2,a)!=0&&sends==0);a[2]="1";a[3]="extra";assert(control_main(4,a)!=0&&sends==0);
 fail_queue=1;assert(control_main(3,a)!=0&&sends==0);fail_queue=0;fail_send=1;assert(control_main(3,a)!=0&&sends==1);fail_send=0;
 for(int i=0;i<2;i++){a[1]=i?"resume":"clean";sends=waits=0;waited=0;assert(control_main(3,a)==0&&sends==3&&waited==500000000L);assert(!memcmp(frames[0],frames[2],6));assert(!memcmp(frames[0],expected[0],6));assert(!memcmp(frames[1],"\xaa\x03\x02\x22\x02\x26",6));sends=waits=0;waited=0;assert(control_main(2,a)==0&&sends==3&&waited==500000000L);assert(!memcmp(frames[0],frames[2],6));assert(!memcmp(frames[0],expected[2],6));}
 for(int i=1;i<=3;i++){fail_send=i;sends=waits=0;waited=0;assert(control_main(3,a)!=0&&sends==i);if(i<3)assert(waits==0);}
 puts("setfan: single-frame, validation, transport errors and clean/resume regression PASS");return 0;
}
