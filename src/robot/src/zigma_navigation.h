/* Local navigation helpers. No consuming reads from firmware IPC queues. */
#include <stdbool.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/file.h>
#include "cJSON.h"
static volatile sig_atomic_t nav_cancelled;
static void nav_signal(int sig){(void)sig;nav_cancelled=1;}
static const char *nav_path(const char *env,const char *fallback){const char *v=getenv(env);return v&&*v?v:fallback;}
static cJSON *nav_read(const char *file){
 FILE *f=fopen(file,"rb");if(!f)return NULL;char *b=malloc(2*1024*1024+1);if(!b){fclose(f);return NULL;}size_t n=fread(b,1,2*1024*1024,f);bool ok=!ferror(f)&&feof(f);fclose(f);b[n]=0;cJSON *j=ok?cJSON_ParseWithLengthOpts(b,n+1,NULL,1):NULL;free(b);return j;
}
static cJSON *nv(const cJSON *j,const char *k){return cJSON_GetObjectItemCaseSensitive(j,k);}
static double nn(const cJSON *j,const char *k){cJSON *v=nv(j,k);return cJSON_IsNumber(v)?v->valuedouble:NAN;}
static const char *ns(const cJSON *j,const char *k){cJSON *v=nv(j,k);return cJSON_IsString(v)?v->valuestring:"";}
static bool nav_int(const char *s,int lo,int hi,int *out){char *end;errno=0;long v=strtol(s,&end,10);if(!*s||*end||errno||v<lo||v>hi)return false;*out=(int)v;return true;}
static cJSON *nav_status(void){return nav_read(nav_path("ZIGMA_STATUS_FILE","/tmp/zigma-free/observation/status.json"));}
static bool nav_recent(double stamp){double age=(double)time(NULL)-stamp;return isfinite(stamp)&&age>=0&&age<=15;}
static bool nav_link(cJSON *s){return nav_recent(nn(s,"cache_read_at"))&&nav_recent(nn(s,"local_link_received_at"));}
static bool nav_idle(cJSON *s){const char *w=ns(s,"working_status");return !strcmp(w,"Standby")||!strcmp(w,"Pause")||!strcmp(w,"CleanDone")||!strcmp(w,"PileCharging")||!strcmp(w,"DirCharging");}
static int nav_json(long type,const char *json){
 struct {long type;char text[4096];} m={type,{0}};snprintf(m.text,sizeof m.text,"%s",json);key_t key=ftok("/",1);int q=key==(key_t)-1?-1:msgget(key,0);
 if(q<0||msgsnd(q,&m,sizeof m.text,IPC_NOWAIT)){perror("Command not dispatched");return 1;}return 0;
}
static double nav_clock(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static int nav_volume(bool set,int value){
 cJSON *s=nav_status();double v=nn(s,"volume");bool valid=nav_link(s)&&isfinite(v)&&v>=0&&v<=100&&floor(v)==v;cJSON_Delete(s);
 if(!valid){fprintf(stderr,"Current volume unavailable; no change sent\n");return 3;}
 if(!set){printf("%.0f\n",v);return 0;}
 char json[160];snprintf(json,sizeof json,"{\"ProtocolVersion\":\"1.0\",\"Volume\":%d,\"cmd_origin\":\"app\"}",value);
 time_t sent=time(NULL);if(nav_json(4,json))return 1;
 double until=nav_clock()+15;while(nav_clock()<until){s=nav_status();bool ok=nav_link(s)&&nn(s,"cache_read_at")>sent&&nn(s,"volume")==value;cJSON_Delete(s);if(ok)return 0;sleep(1);}
 fprintf(stderr,"Volume dispatched but not confirmed\n");return 4;
}
static bool nav_map(cJSON *m,int x,int y){
 cJSON *o=nv(m,"map_origin"),*ox=cJSON_GetArrayItem(o,0),*oy=cJSON_GetArrayItem(o,1);
 return cJSON_IsTrue(nv(m,"available"))&&*ns(m,"map_id")&&cJSON_IsNumber(ox)&&cJSON_IsNumber(oy)&&isfinite(ox->valuedouble)&&isfinite(oy->valuedouble)&&x+ox->valuedouble>=0&&y+oy->valuedouble>=0&&x+ox->valuedouble<nn(m,"width")&&y+oy->valuedouble<nn(m,"height");
}
static bool nav_same_map(cJSON *a,cJSON *b){return *ns(a,"map_id")&&!strcmp(ns(a,"map_id"),ns(b,"map_id"))&&nn(a,"width")==nn(b,"width")&&nn(a,"height")==nn(b,"height")&&cJSON_Compare(nv(a,"map_origin"),nv(b,"map_origin"),1);}
/* Return success only after the requested state was seen, followed by stable
 * destination/charging evidence. Cache receipt is not a sensor timestamp. */
static int nav_progress(cJSON *s,cJSON *m,int x,int y,bool dock,bool *started){
 const char *w=ns(s,"working_status");
 if(!nav_link(s))return -3;
 if(strcmp(ns(s,"error"),"NoError"))return -5;
 if(dock){if(!strcmp(w,"BackCharging"))*started=true;if(*started&&strcmp(w,"BackCharging")&&strcmp(w,"PileCharging")&&strcmp(w,"Relocation")&&strcmp(w,"DirCharging"))return -5;return *started&&!strcmp(w,"PileCharging")?1:0;}
 if(!strcmp(w,"PlanningLocation"))*started=true;
 if(!*started)return 0;
 if(!strcmp(w,"PlanningLocation")||!strcmp(w,"Relocation"))return 0;
 if(strcmp(w,"Standby"))return -5;
 cJSON *p=nv(s,"navigation_pose");if(!nav_recent(nn(p,"cache_read_at"))||!nav_same_map(m,p))return -3;
 cJSON *o=nv(m,"map_origin");double px=x+cJSON_GetArrayItem(o,0)->valuedouble,py=y+cJSON_GetArrayItem(o,1)->valuedouble;
 double dx=nn(p,"x")-px,dy=nn(p,"y")-py;
 return isfinite(dx)&&isfinite(dy)&&hypot(dx,dy)<=3?1:-5;
}
/* Pause is a fan setting, not the robot's motion-pause command. */
static int nav_fan_pause(void){return nav_json(1,"{\"ProtocolVersion\":\"1.0\",\"FanStatus\":\"Pause\",\"cmd_origin\":\"app\"}");}
/* A bounded watcher for nonblocking goto; never retries or stops the robot. */
static int nav_watch_start(cJSON *map,const char *mapfile,time_t sent){
 double deadline=nav_clock()+15;
 while(nav_clock()<deadline&&!nav_cancelled){
  sleep(1);cJSON *s=nav_status(),*m=nav_read(mapfile);
  bool valid=nav_link(s)&&!strcmp(ns(s,"error"),"NoError")&&nav_same_map(map,m);cJSON_Delete(m);
  if(!valid){cJSON_Delete(s);return 3;}
  bool active=nn(s,"cache_read_at")>sent&&!strcmp(ns(s,"working_status"),"PlanningLocation");
  bool waiting=nav_idle(s)||!strcmp(ns(s,"working_status"),"Relocation");cJSON_Delete(s);
  if(active)return nav_fan_pause();
  if(!waiting)return 5;
 }
 return nav_cancelled?130:4;
}
static int nav_background_start(int lock,cJSON *map,const char *mapfile,time_t sent){
 pid_t child=fork();if(child<0){perror("Cannot start fan watcher");return 1;}
 if(child)return 0;
 /* Keep only the shared navigation lock. Do not hold caller pipes/sockets. */
 setsid();long maxfd=sysconf(_SC_OPEN_MAX);if(maxfd<0)maxfd=1024;
 for(int fd=0;fd<maxfd;fd++)if(fd!=lock)close(fd);
 int nullfd=open("/dev/null",O_RDWR);if(nullfd>=0){dup2(nullfd,0);dup2(nullfd,1);dup2(nullfd,2);if(nullfd>2&&nullfd!=lock)close(nullfd);}
 signal(SIGINT,nav_signal);signal(SIGTERM,nav_signal);
 int rc=nav_watch_start(map,mapfile,sent);
 int log=open(nav_path("ZIGMA_NAV_LOG","/tmp/zigma-navigation.log"),O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC,0600);
 if(log>=0){dprintf(log,"%ld goto fan-start watcher exit=%d\n",(long)time(NULL),rc);close(log);}
 close(lock);_exit(rc);
}
static int nav_move(int x,int y,bool dock,bool block,int timeout){
 nav_cancelled=0;
 int lock=open(nav_path("ZIGMA_NAV_LOCK","/tmp/zigma-navigation.lock"),O_CREAT|O_RDWR|O_CLOEXEC,0600);
 if(lock<0||flock(lock,LOCK_EX|LOCK_NB)){if(lock>=0)close(lock);fprintf(stderr,"Another navigation command is running\n");return 6;}
 const char *mapfile=nav_path("ZIGMA_MAP_FILE","/tmp/zigma-free/observation/map.json");
 cJSON *state=nav_read(nav_path("ZIGMA_STATE_FILE","/root/zigma-free/data/state.json"));
 cJSON *pending=nv(state,"map_reset_pending");const char *phase=ns(pending,"phase");
 bool changing=cJSON_IsObject(pending)&&strcmp(phase,"not_sent")&&strcmp(phase,"new_map_observed");cJSON_Delete(state);
 if(!dock&&changing){close(lock);fprintf(stderr,"Map change pending; destination not sent\n");return 3;}
 cJSON *m=dock?NULL:nav_read(mapfile),*s=nav_status();int rc=3;
 if(!nav_link(s)||strcmp(ns(s,"error"),"NoError")||(!dock&&!nav_idle(s))){fprintf(stderr,"Idle robot and recent local communication required\n");goto done;}
 if(!dock&&!nav_map(m,x,y)){fprintf(stderr,"Destination is outside the current map or map unavailable\n");goto done;}
 if(dock&&block&&!strcmp(ns(s,"working_status"),"PileCharging")){rc=0;goto done;}
 /* Native numeric FanStatus uses percent command 0x39, unlike None (0x24/11).
  * Physical effect throughout navigation still needs supervised validation. */
 char json[256];snprintf(json,sizeof json,"{\"ProtocolVersion\":\"1.0\",\"FanStatus\":\"0\",\"goto_point\":[%d,%d],\"cmd_origin\":\"app\"}",x,y);
 time_t sent=time(NULL);rc=dock?send_command(2,0x22,3):nav_json(1,json);if(rc)goto done;
 if(!block){if(!dock)rc=nav_background_start(lock,m,mapfile,sent);goto done;}
 signal(SIGINT,nav_signal);signal(SIGTERM,nav_signal);
 double deadline=nav_clock()+timeout,stable=0;bool started=false,fan_paused=false;double last=0;rc=4;
 while(nav_clock()<deadline&&!nav_cancelled){
  sleep(1);cJSON_Delete(s);s=nav_status();
  if(!dock){cJSON *now=nav_read(mapfile);bool same=nav_same_map(m,now);cJSON_Delete(now);if(!same){rc=5;break;}}
  if(!nav_link(s)){rc=3;break;}
  double sample=nn(s,"cache_read_at");if(sample<=sent||sample<=last)continue;last=sample;
  int progress=nav_progress(s,m,x,y,dock,&started);if(progress<0){rc=-progress;break;}
  if(!dock&&!fan_paused&&!strcmp(ns(s,"working_status"),"PlanningLocation")){if(nav_fan_pause()){rc=1;break;}fan_paused=true;}
  if(progress==1){if(!stable)stable=nav_clock();if(nav_clock()-stable>=(dock?5:1)){rc=0;break;}}else stable=0;
 }
 if(nav_cancelled)rc=130;
 if(rc){send_command(2,0x26,0);fprintf(stderr,"Navigation unconfirmed (exit %d); stop requested\n",rc);}
 done:cJSON_Delete(s);cJSON_Delete(m);close(lock);return rc;
}
