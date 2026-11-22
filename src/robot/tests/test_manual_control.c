#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#define main control_main
#define time fake_time
#define clock_gettime fake_clock_gettime
#define nanosleep fake_nanosleep
#define msgget fake_msgget
#define msgsnd fake_msgsnd
#include "../src/zigmactl.c"
#undef main
static long ms;
static int sends, fail_at, scenario;
static bool supported=true,alive=true;
static char messages[128][64],statusfile[256];
static struct manual_sample sample;
static int events,phase,block_phase,lease_kind;
static long effect_at,timer_at;
static bool saw_direction;
static bool sampling(struct manual_sample *s){
 if(!alive)return false;
 if(timer_at>=0&&ms>=timer_at){sample.timer=0;timer_at=-1;}
 if(phase==1&&ms>=effect_at&&block_phase!=1){sample.neutral=true;sample.empty=true;sample.last=0xa1;}
 if(phase==2&&ms>=effect_at&&block_phase!=2){sample.state=26;sample.app_state=2;sample.empty=true;}
 *s=sample;return true;
}
static int event(bool releasing,double deadline){
 assert(deadline>fake_time(NULL)-1000);events++;
 if(block_phase==3)return 5;
 if(releasing){assert(sample.state==7&&!sample.timer);phase=1;sample.empty=false;effect_at=ms+40;}
 else{assert(sample.neutral&&sample.empty&&sample.last==0xa1);phase=2;sample.empty=false;effect_at=ms+60;}
 return 0;
}
static int lease(double *until){
 if(lease_kind==1){if(ms>=900)return 2;if(ms%200==0){*until=ms/1000.0+0.75;return 1;}}
 if(lease_kind==2&&ms==0){*until=0.75;return 1;}
 if(lease_kind==3)return 2;
 if(lease_kind==4){if(ms>=12000)return 2;if(ms%200==0){*until=ms/1000.0+0.75;return 1;}}
 return 0;
}
static bool profile(void){return supported;}
static bool living(void){return alive;}
time_t fake_time(time_t *out){time_t t=1000+ms/1000;if(out)*out=t;return t;}
int fake_clock_gettime(clockid_t id,struct timespec *t){(void)id;t->tv_sec=ms/1000;t->tv_nsec=(ms%1000)*1000000;return 0;}
int fake_msgget(key_t k,int f){(void)k;assert(f==0);return 9;}
int fake_msgsnd(int q,const void *p,size_t n,int f){assert(q==9&&n==4096&&f==IPC_NOWAIT);assert(*(const long*)p==1);cJSON *j=cJSON_Parse((const char*)p+sizeof(long));assert(j);assert(!strcmp(ns(j,"ProtocolVersion"),"1.0"));assert(sends<128);snprintf(messages[sends++],64,"%s",ns(j,"WorkStatus"));if(!strncmp(messages[sends-1],"Move",4)&&strcmp(messages[sends-1],"MoveStop")){
 sample.state=7;sample.app_state=21;sample.active=1;sample.last=0x9d;sample.timer=1;sample.neutral=false;saw_direction=true;
 }
 if(!strcmp(messages[sends-1],"MoveStop")&&sample.timer&&block_phase!=4)timer_at=ms+60;
 cJSON_Delete(j);if(sends==fail_at){errno=EAGAIN;return -1;}return 0;}
static void status(const char *w,const char *error,int age){FILE *f=fopen(statusfile,"w");assert(f);fprintf(f,"{\"working_status\":\"%s\",\"error\":\"%s\",\"cache_read_at\":%ld,\"local_link_received_at\":%ld}",w,error,(long)fake_time(NULL)-age,(long)fake_time(NULL)-age);fclose(f);}
int fake_nanosleep(const struct timespec *a,struct timespec *b){(void)b;ms+=a->tv_sec*1000+a->tv_nsec/1000000;if(scenario&&ms>=200){int s=scenario;scenario=0;if(s==1)assert(manual_stop()==0);if(s==2)raise(SIGINT);if(s==3)alive=false;if(s==4)status("Standby","WheelSuspendInMidair",0);}return 0;}
static void reset(void){ms=0;sends=0;fail_at=0;scenario=0;alive=true;supported=true;events=0;phase=0;block_phase=0;lease_kind=0;timer_at=-1;saw_direction=false;
 sample=(struct manual_sample){.state=0,.app_state=2,.neutral=true,.empty=true};status("Standby","NoError",0);}
static void stopped(void){assert(sends>=2&&!strcmp(messages[sends-2],"MoveStop")&&!strcmp(messages[sends-1],"Pause"));}
int main(void){
 char dir[]="/tmp/manual-test-XXXXXX";assert(mkdtemp(dir));char gate[256],lock[256];snprintf(statusfile,sizeof statusfile,"%s/status",dir);snprintf(gate,sizeof gate,"%s/gate",dir);snprintf(lock,sizeof lock,"%s/lock",dir);setenv("ZIGMA_STATUS_FILE",statusfile,1);setenv("ZIGMA_MANUAL_GATE",gate,1);setenv("ZIGMA_NAV_LOCK",lock,1);manual_profile_check=profile;manual_alive_check=living;manual_read_sample=sampling;manual_event=event;manual_lease=lease;
 const char *directions[]={"forward","backward","left","right"},*native[]={"MoveFront","MoveBack","MoveLeft","MoveRight"};
 for(int i=0;i<4;i++){reset();char *args[]={"zigmactl","drive",(char*)directions[i],"1200"};assert(control_main(4,args)==0);assert(ms>=1200&&ms<=1700&&sends==5&&events==2);for(int n=0;n<3;n++)assert(!strcmp(messages[n],native[i]));stopped();}
 /* CleanDone must pass both observer and native checks, without weakening guards. */
 reset();status("CleanDone","NoError",0);sample.app_state=70;sample.state=26;
 assert(manual_check()==0&&sends==0&&events==0);
 assert(manual_drive("MoveFront",200)==0&&events==2);stopped();
 for(int guard=0;guard<7;guard++){
  reset();status("CleanDone","NoError",0);sample.app_state=70;sample.state=26;
  switch(guard){case 0:sample.timer=1;break;case 1:sample.neutral=false;break;
   case 2:sample.empty=false;break;case 3:sample.state=7;break;
   case 4:sample.app_error=1;break;case 5:sample.app_state=4;break;
   case 6:status("CleanDone","NoError",16);break;}
  if(guard<6)assert(manual_initial_reason(&sample)!=NULL);
  assert(manual_check()==3&&sends==0&&events==0);
  assert(manual_drive("MoveFront",200)==3&&sends==0&&events==0);
 }
 const char *durations[]={"0","99","10001","2m","nan","1.5","nans","infs","0s","11s"};for(unsigned i=0;i<sizeof durations/sizeof *durations;i++){reset();char *args[]={"zigmactl","drive","forward",(char*)durations[i]};assert(control_main(4,args)==2&&sends==0);}
 reset();char *bad[]={"zigmactl","drive"};assert(control_main(2,bad)==2&&sends==0);
 const char *states[]={"Hibernating","PileCharging","AutoClean","DirectionControl"};for(unsigned i=0;i<4;i++){reset();status(states[i],"NoError",0);assert(manual_drive("MoveFront",1000)==3&&sends==0);}
 reset();status("Standby","NoError",16);assert(manual_drive("MoveFront",1000)==3&&sends==0);
 reset();supported=false;assert(manual_drive("MoveFront",1000)==7&&sends==0);
 for(int s=1;s<=4;s++){reset();scenario=s;assert(manual_drive("MoveFront",2000)==(s<=2?130:s==3?8:3));assert(sends==3);stopped();}
 reset();fail_at=1;assert(manual_drive("MoveFront",1000)==1&&sends==3);stopped();
 reset();fail_at=1;assert(manual_stop()==1&&sends==2);stopped();
 reset();status("Hibernating","NoError",100);sample.app_state=3;assert(manual_stop()==8&&sends==2&&events==0);stopped();
 reset();int fd=open(lock,O_RDWR|O_CREAT,0600);assert(fd>=0&&!flock(fd,LOCK_EX));assert(manual_drive("MoveFront",1000)==6&&sends==0);close(fd);
 reset();char *seconds[]={"zigmactl","drive","forward","1.2s"};assert(control_main(4,seconds)==0&&ms<=1700&&events==2);
 for(int b=1;b<=3;b++){reset();block_phase=b;assert(manual_drive("MoveFront",200)==8);assert(ms<=2800);assert(events==(b==2?2:1));}
 reset();block_phase=4;assert(manual_drive("MoveFront",200)==8&&events==0&&ms<=820);
 reset();lease_kind=1;assert(manual_run("MoveFront",0,true)==0);assert(ms>=900&&ms<1400&&events==2);
 reset();lease_kind=2;assert(manual_run("MoveFront",0,true)==0);assert(ms>=750&&ms<1300&&events==2);
 reset();lease_kind=3;assert(manual_run("MoveFront",0,true)==0&&sends==0&&events==0);
 reset();lease_kind=4;assert(manual_run("MoveFront",0,true)==0&&ms>=12000&&ms<12500&&events==2);
 /* Actual pipe parser: renewal, explicit release, invalid frame and EOF. */
 int pipes[2],saved=dup(0);assert(saved>=0&&!pipe(pipes)&&dup2(pipes[0],0)>=0);double expires=0;
 assert(write(pipes[1],".\n",2)==2);assert(manual_lease_native(&expires)==1);
 assert(write(pipes[1],"!",1)==1);assert(manual_lease_native(&expires)==2);
 assert(write(pipes[1],"x",1)==1);assert(manual_lease_native(&expires)==-1);
 close(pipes[1]);assert(manual_lease_native(&expires)==2);assert(dup2(saved,0)>=0);close(saved);close(pipes[0]);
 unlink(statusfile);unlink(gate);unlink(lock);assert(!rmdir(dir));puts("manual-control tests PASS");return 0;
}
