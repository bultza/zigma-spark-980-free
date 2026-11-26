#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#define main control_main
#include "../src/zigmactl.c"
#undef main
struct fake {double t,paused,first_pause,last_send;int initial,mode,mutes,unmutes,sends,cleans,pauses,fail_send,fail_mute,fail_unmute;};
static double clock_fake(void *v){return ((struct fake *)v)->t;}
static struct wake_snapshot snapshot_fake(void *v){
 struct fake *f=v;enum wake_state s=f->initial;
 if(f->cleans){s=WAKE_ASLEEP;if(f->mode!=1&&f->t>=100.8)s=WAKE_ACTIVE;}
 if(f->pauses&&f->mode!=1)s=WAKE_IDLE;
 if(f->mode==2&&f->cleans)s=WAKE_UNKNOWN;
 /* Intermediate awake-idle must not pull pause ahead of command processing;
  * model the first pause being ignored and only the second taking effect. */
 if(f->mode==6&&f->cleans&&f->pauses<2)s=f->t<101?WAKE_IDLE:WAKE_ACTIVE;
 double gen=f->mode==3?99:f->mode==5?floor(f->t/2.5)*2.5:floor(f->t*2)/2;
 if(f->mode==5&&f->pauses&&gen<f->paused+3.0)s=WAKE_ACTIVE;
 return (struct wake_snapshot){s,gen};
}
static int mute_fake(void *v){struct fake *f=v;f->mutes++;return f->fail_mute;}
static int unmute_fake(void *v){struct fake *f=v;f->unmutes++;return f->fail_unmute;}
static int send_fake(void *v,unsigned char op,unsigned char arg){
 struct fake *f=v;assert(f->mutes==1);f->sends++;f->last_send=f->t;assert(f->t<=103.51);
 if(op==0x22){assert(arg==2);f->cleans++;assert(f->cleans==1);}
 else if(op==0x26){assert(arg==0);f->pauses++;assert(f->pauses<=2);if(f->pauses==1)f->first_pause=f->t;f->paused=f->t;}
 else {assert(op==0x24&&arg==2);}
 return f->sends==f->fail_send;
}
static int alive_fake(void *v){struct fake *f=v;return !((f->mode==4&&f->t>=100.3)||(f->mode==7&&f->t>=104));}
static void wait_fake(void *v,int ms){((struct fake *)v)->t+=ms/1000.0;}
static int run(struct fake *f){f->t=100;struct wake_ops o={f,clock_fake,snapshot_fake,mute_fake,unmute_fake,send_fake,alive_fake,wait_fake};return wake_run(&o);}
int main(void){
 struct fake f={.initial=WAKE_ASLEEP};assert(run(&f)==0&&f.cleans==1&&f.pauses==2&&f.unmutes==1&&fabs(f.first_pause-102.0)<0.01&&fabs(f.paused-103.5)<0.01&&f.t>=f.paused+2);
 for(int i=WAKE_IDLE;i<=WAKE_ACTIVE;i++){f=(struct fake){.initial=i};assert(run(&f)==0&&!f.mutes&&!f.sends);}
 f=(struct fake){.initial=WAKE_UNKNOWN};assert(run(&f)==3&&!f.mutes&&!f.sends);
 f=(struct fake){.initial=WAKE_ASLEEP,.fail_mute=1};assert(run(&f)==5&&!f.sends&&!f.unmutes);
 f=(struct fake){.initial=WAKE_ASLEEP,.fail_unmute=1};assert(run(&f)==7&&f.unmutes==1);
 for(int i=1;i<=5;i++){f=(struct fake){.initial=WAKE_ASLEEP,.fail_send=i};assert(run(&f)==5&&f.unmutes==1);assert(f.cleans<=1);if(i>=2)assert(f.pauses==2);}
 for(int i=1;i<=4;i++){f=(struct fake){.initial=WAKE_ASLEEP,.mode=i};int rc=run(&f);assert(rc==(i==2?3:i==4?130:4));assert(f.cleans==1&&f.pauses==2&&f.unmutes==1&&f.t<116);}
 /* Slow publication plus transition latency must not cause a false timeout. */
 f=(struct fake){.initial=WAKE_ASLEEP,.mode=5};assert(run(&f)==0&&f.cleans==1&&f.pauses==2&&f.unmutes==1);
 assert(fabs(f.first_pause-102.0)<0.01&&fabs(f.paused-103.5)<0.01&&f.t>f.paused+5&&f.t<f.paused+10);
 /* Missing confirmation must never postpone or add a stop command. */
 f=(struct fake){.initial=WAKE_ASLEEP,.mode=1};assert(run(&f)==4&&f.pauses==2&&fabs(f.last_send-103.5)<0.01&&f.t>=113.5);
 f=(struct fake){.initial=WAKE_ASLEEP,.mode=6};assert(run(&f)==0&&f.pauses==2&&fabs(f.first_pause-102.0)<0.01&&fabs(f.last_send-103.5)<0.01);
 f=(struct fake){.initial=WAKE_ASLEEP,.mode=7};assert(run(&f)==130&&f.pauses==2&&fabs(f.last_send-103.5)<0.01);
 cJSON *s=cJSON_CreateObject();
 cJSON_AddNumberToObject(s,"cache_read_at",time(NULL));cJSON_AddNumberToObject(s,"local_link_received_at",time(NULL));
 cJSON_AddNumberToObject(s,"observer_monotonic_ms",100000);cJSON_AddStringToObject(s,"working_status_source","robot_work_state_cache");
 cJSON_AddStringToObject(s,"working_status","Hibernating");cJSON_AddStringToObject(s,"error","NoError");
 cJSON_AddNumberToObject(s,"vendor_state_code",4);cJSON_AddNumberToObject(s,"working_state_code",4);
 assert(wake_decode(s,101).state==WAKE_ASLEEP);assert(wake_decode(s,106).state==WAKE_UNKNOWN);assert(wake_decode(s,99).state==WAKE_UNKNOWN);
 cJSON_ReplaceItemInObject(s,"error",cJSON_CreateString("VendorError81"));assert(wake_decode(s,101).state==WAKE_UNKNOWN);
 cJSON_ReplaceItemInObject(s,"vendor_state_code",cJSON_CreateNumber(70));assert(wake_decode(s,101).state==WAKE_ASLEEP);
 cJSON_ReplaceItemInObject(s,"local_link_received_at",cJSON_CreateNumber(time(NULL)-30));assert(wake_decode(s,101).state==WAKE_UNKNOWN);
 cJSON_Delete(s);puts("wake: state, timeout, stale evidence, cancellation, failures and cleanup PASS");
}
