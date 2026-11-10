#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include "zigma_version.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>
#include "cJSON.h"

#define LIMIT 32768
#define MAX_JOBS 24
#define BEGIN "# BEGIN ZIGMA LOCAL SCHEDULES"
#define END "# END ZIGMA LOCAL SCHEDULES"
static char data_dir[512], web_dir[512], token[129], executable[512];
static const char *bind_ip="127.0.0.1", *robot="Caccamora", *mode="demo";
static const char *settings_bin=NULL,*area_bin=NULL,*zones_bin=NULL;
static const char *maps_bin=NULL,*maps_dir=NULL,*map_library_dir=NULL,*maps_app_pid=NULL;
static char current_zone[128]="Europe/Madrid";
static const char *control_bin=NULL, *status_file=NULL, *map_file=NULL;
static const char *cron_dir=NULL, *cron_bin="/usr/bin/crontab", *cron_helper=NULL;
static int port=8765, listener=-1;
static volatile sig_atomic_t running=1;
static cJSON *state;
static time_t action_at;
static char activity[32]="charging";

static void fatal(const char *s) { perror(s); exit(1); }
static cJSON *get(cJSON *o,const char *k) { return cJSON_GetObjectItemCaseSensitive(o,k); }
static const char *str(cJSON *o,const char *k,const char *fallback) {
 cJSON *v=get(o,k); return cJSON_IsString(v)?v->valuestring:fallback;
}
static int num(cJSON *o,const char *k,int fallback) {
 cJSON *v=get(o,k); return cJSON_IsNumber(v)?v->valueint:fallback;
}
static void setstr(cJSON *o,const char *k,const char *v) {
 cJSON_DeleteItemFromObjectCaseSensitive(o,k); cJSON_AddStringToObject(o,k,v);
}
static void setnum(cJSON *o,const char *k,double v) {
 cJSON_DeleteItemFromObjectCaseSensitive(o,k); cJSON_AddNumberToObject(o,k,v);
}
static void setbool(cJSON *o,const char *k,bool v) {
 cJSON_DeleteItemFromObjectCaseSensitive(o,k); cJSON_AddBoolToObject(o,k,v);
}
static void path(char *out,size_t n,const char *name) {
 if(snprintf(out,n,"%s/%s",data_dir,name)>=(int)n) { errno=ENAMETOOLONG; fatal("data path"); }
}
static char *read_file(const char *p,size_t max) {
 FILE *f=fopen(p,"rb"); if(!f)return NULL;
 char *s=calloc(max+1,1); if(!s)fatal("calloc");
 size_t n=fread(s,1,max,f); int extra=fgetc(f); bool bad=ferror(f)||extra!=EOF;
 fclose(f); if(bad) { free(s); return NULL; } s[n]=0; return s;
}
static bool atomic_write(const char *p,const char *s) {
 char tmp[1024]; if(snprintf(tmp,sizeof(tmp),"%s.tmp.%ld",p,(long)getpid())>=(int)sizeof(tmp))return false;
 int fd=open(tmp,O_WRONLY|O_CREAT|O_EXCL,0600); if(fd<0)return false;
 size_t n=strlen(s),off=0; bool ok=true;
 while(off<n) { ssize_t r=write(fd,s+off,n-off); if(r<=0) {ok=false;break;} off+=(size_t)r; }
 if(ok&&fsync(fd))ok=false;
 if(close(fd))ok=false;
 if(ok&&rename(tmp,p))ok=false;
 if(!ok)unlink(tmp);
 return ok;
}
static bool save_state(void) {
 char p[1024]; path(p,sizeof(p),"state.json"); char *s=cJSON_PrintUnformatted(state);
 bool ok=s&&atomic_write(p,s); free(s);return ok;
}
/* Depth is bounded by the parser; array width must not limit valid paths. */
static bool unique_keys(const cJSON *n,unsigned depth) {
 if(depth>CJSON_NESTING_LIMIT)return false;
 if(cJSON_IsObject(n))for(const cJSON *a=n->child;a;a=a->next)
  for(const cJSON *b=a->next;b;b=b->next)
   if(a->string&&b->string&&!strcmp(a->string,b->string))return false;
 for(const cJSON *c=n->child;c;c=c->next)
  if(c->child&&!unique_keys(c,depth+1))return false;
 return true;
}
static cJSON *strict_json(const char *s) {
 const char *end=NULL;cJSON *o=cJSON_ParseWithOpts(s,&end,1);
 if(o&&!unique_keys(o,0)){cJSON_Delete(o);return NULL;}
 return o;
}
static void event(const char *kind,const char *message) {
 cJSON *events=get(state,"events"),*e=cJSON_CreateObject();
 setnum(state,"sequence",num(state,"sequence",0)+1);
 cJSON_AddNumberToObject(e,"id",num(state,"sequence",0));
 cJSON_AddNumberToObject(e,"timestamp",(double)time(NULL));
 cJSON_AddStringToObject(e,"kind",kind);cJSON_AddStringToObject(e,"message",message);
 cJSON_InsertItemInArray(events,0,e);while(cJSON_GetArraySize(events)>80)cJSON_DeleteItemFromArray(events,80);
}
static bool safe_path(const char *s) {
 if(!s||s[0]!='/')return false;
 for(;*s;s++)if(!isalnum((unsigned char)*s)&&!strchr("/._-",*s))return false;
 return true;
}
static int run_process(char *const argv[],char *output,size_t cap) {
 int pipes[2];if(pipe(pipes))return -1;
 pid_t child=fork(); if(child<0){close(pipes[0]);close(pipes[1]);return -1;}
 if(!child){dup2(pipes[1],STDOUT_FILENO);int null=open("/dev/null",O_RDWR);dup2(null,STDIN_FILENO);dup2(null,STDERR_FILENO);close(pipes[0]);close(pipes[1]);execv(argv[0],argv);_exit(127);}
 close(pipes[1]);fcntl(pipes[0],F_SETFL,O_NONBLOCK);size_t used=0;int status=0;bool done=false;
 struct timespec start,now;clock_gettime(CLOCK_MONOTONIC,&start);
 while(!done){
  char buf[1024];ssize_t n;
  while((n=read(pipes[0],buf,sizeof(buf)))>0){if(output&&used+(size_t)n<cap){memcpy(output+used,buf,(size_t)n);used+=(size_t)n;}else if(output){kill(child,SIGKILL);waitpid(child,NULL,0);close(pipes[0]);return -1;}}
  pid_t w=waitpid(child,&status,WNOHANG);if(w==child)done=true;
  else if(w<0){close(pipes[0]);return -1;}
  clock_gettime(CLOCK_MONOTONIC,&now);
  if(!done&&now.tv_sec-start.tv_sec>=4){kill(child,SIGKILL);waitpid(child,&status,0);close(pipes[0]);return -1;}
  if(!done){struct timespec pause={0,10000000};nanosleep(&pause,NULL);}
 }
 // Drain after the child exits.
 if(output){ssize_t n;while((n=read(pipes[0],output+used,cap-used-1))>0)used+=(size_t)n;output[used]=0;}
 close(pipes[0]);return WIFEXITED(status)?WEXITSTATUS(status):-1;
}
static void now_local(struct tm *tm,time_t *epoch) { *epoch=time(NULL);localtime_r(epoch,tm); }
static bool valid_time(const char *s) {
 return strlen(s)==5&&isdigit((unsigned char)s[0])&&isdigit((unsigned char)s[1])&&s[2]==':'&&isdigit((unsigned char)s[3])&&isdigit((unsigned char)s[4])&&atoi(s)<24&&atoi(s+3)<60;
}
static bool has_day(cJSON *job,int day) {
 cJSON *v; cJSON_ArrayForEach(v,get(job,"days"))if(v->valueint==day)return true;return false;
}
static cJSON *map_library(const char *operation,cJSON *input);
static cJSON *restore_helper(const char *op,cJSON *input);
static bool restore_master(int fd,cJSON *input);
static bool random_uid(char text[33]){unsigned char bytes[16];int fd=open("/dev/urandom",O_RDONLY);bool ok=fd>=0&&read(fd,bytes,16)==16;if(fd>=0)close(fd);if(!ok)return false;for(int i=0;i<16;i++)snprintf(text+2*i,3,"%02x",bytes[i]);return true;}
/* Missing power preserves legacy callers: maximum suction. */
static int fan_power(cJSON *body){
 cJSON *v=get(body,"fan_power");
 if(!v)return 3;
 return cJSON_IsNumber(v)&&v->valuedouble==v->valueint&&v->valueint>=1&&v->valueint<=3?v->valueint:0;
}
static cJSON *schedule_master(const char *uid){
 cJSON *request=cJSON_CreateObject(),*library=map_library("list",request),*found=NULL,*entry;cJSON_Delete(request);
 cJSON_ArrayForEach(entry,get(library,"items"))if(!strcmp(str(entry,"uid",""),uid)&&!cJSON_IsTrue(get(entry,"deleted"))&&cJSON_IsTrue(get(entry,"verified")))found=cJSON_Duplicate(entry,1);
 cJSON_Delete(library);return found;
}
static cJSON *cron_health(bool ensure){
 if(!cron_dir)return NULL;
 cJSON *o=NULL;char buf[2048]={0};if(cron_helper){char *args[]={(char*)cron_helper,ensure?"ensure":"status",NULL};if(!run_process(args,buf,sizeof(buf)))o=strict_json(buf);}
 else{o=cJSON_CreateObject();setbool(o,"available",access(cron_bin,X_OK)==0);setbool(o,"running",true);setbool(o,"test_adapter",true);}
 return o;
}
#include "zigma_schedule_commands.h"
#include "zigma_cron_view.h"
static bool direct_control(cJSON *job){const char *action=str(job,"action","clean");return strcmp(mode,"demo")&&control_bin&&(!strcmp(action,"reboot")||!strcmp(action,"clean")||schedule_extra(job));}
static cJSON *validate_jobs(cJSON *input,char *error,size_t cap) {
 if(!cJSON_IsArray(input)||cJSON_GetArraySize(input)>MAX_JOBS){snprintf(error,cap,"Maximum 24 schedules");return NULL;}
 cJSON *out=cJSON_CreateArray(),*j;int next=num(state,"next_schedule_id",1),seen[MAX_JOBS],count=0;
 cJSON_ArrayForEach(j,input){
  const char *label=str(j,"label",""),*t=str(j,"time","");cJSON *days=get(j,"days"),*enabled=get(j,"enabled"),*id=get(j,"id");
  if(!cJSON_IsObject(j)||strlen(label)<1||strlen(label)>72||!valid_time(t)||!cJSON_IsBool(enabled)||!cJSON_IsArray(days)||cJSON_GetArraySize(days)<1||cJSON_GetArraySize(days)>7){snprintf(error,cap,"Check schedule name, time and weekdays");goto fail;}
  for(const unsigned char *p=(const unsigned char*)label;*p;p++)if(*p<32){snprintf(error,cap,"Invalid name");goto fail;}
  bool used[7]={0};cJSON *d;cJSON_ArrayForEach(d,days){if(!cJSON_IsNumber(d)||d->valuedouble!=d->valueint||d->valueint<0||d->valueint>6||used[d->valueint]){snprintf(error,cap,"Invalid weekdays");goto fail;}used[d->valueint]=true;}
  const char *action=str(j,"action","clean");bool restore=!strcmp(action,"restore_master");
  if(strcmp(action,"clean")&&strcmp(action,"dock")&&strcmp(action,"reboot")&&!restore&&!schedule_extra(j)){snprintf(error,cap,"Unsupported schedule action");goto fail;}
  if((restore||!strcmp(action,"reboot"))&&!cJSON_IsTrue(get(j,"confirm_action"))){snprintf(error,cap,"Confirm the scheduled reboot or master restoration");goto fail;}
  cJSON *master=restore?schedule_master(str(j,"master_uid","")):NULL;
  if(restore&&cJSON_IsTrue(enabled)&&(!master||(strlen(str(j,"master_revision",""))&&strcmp(str(j,"master_revision",""),str(master,"revision",""))))){cJSON_Delete(master);snprintf(error,cap,"Choose an available, verified master copy");goto fail;}
  cJSON_Delete(master);
  int n;
  if(id&&!cJSON_IsNull(id)){
   if(!cJSON_IsNumber(id)||id->valuedouble!=id->valueint||id->valueint<=0){snprintf(error,cap,"Invalid identifier");goto fail;}
   n=id->valueint;bool exists=false;cJSON *old;cJSON_ArrayForEach(old,get(state,"schedules"))if(num(old,"id",0)==n)exists=true;
   if(!exists){snprintf(error,cap,"Schedule no longer exists; reload the page");goto fail;}
  }else n=next++;
  for(int i=0;i<count;i++) { if(seen[i]==n){snprintf(error,cap,"Identificador repetido");goto fail;} }
  seen[count++]=n;
  cJSON *o=cJSON_CreateObject();cJSON_AddNumberToObject(o,"id",n);cJSON_AddStringToObject(o,"label",label);cJSON_AddStringToObject(o,"time",t);cJSON_AddItemToObject(o,"days",cJSON_Duplicate(days,1));cJSON_AddBoolToObject(o,"enabled",cJSON_IsTrue(enabled));setstr(o,"action",action);setbool(o,"confirm_action",restore||!strcmp(action,"reboot"));
  if(!strcmp(action,"clean")){cJSON *power_source=j;if(!get(j,"fan_power")){cJSON *prior;cJSON_ArrayForEach(prior,get(state,"schedules"))if(num(prior,"id",0)==n&&!strcmp(str(prior,"action","clean"),"clean"))power_source=prior;}int power=fan_power(power_source);if(!power){cJSON_Delete(o);snprintf(error,cap,"Fan power must be 1, 2 or 3");goto fail;}setnum(o,"fan_power",power);}
  if(!strcmp(action,"play_song")){const char *song=str(j,"song","");if(!schedule_song_name(song)){cJSON_Delete(o);snprintf(error,cap,"Choose an MP3 or WAV filename (letters, digits, dot, dash or underscore)");goto fail;}setstr(o,"song",song);}
  if(!strcmp(action,"custom_command")){const char *command=str(j,"command","");if(!schedule_command_valid(command)){cJSON_Delete(o);snprintf(error,cap,"Enter a single-line command, maximum 256 characters");goto fail;}setstr(o,"command",command);}
  if(restore){cJSON *copy=schedule_master(str(j,"master_uid",""));setstr(o,"master_uid",str(j,"master_uid",""));setstr(o,"master_revision",copy?str(copy,"revision",""):str(j,"master_revision",""));setstr(o,"master_name",copy?str(copy,"name",""):str(j,"master_name","Unavailable copy"));cJSON_Delete(copy);}
  cJSON_AddItemToArray(out,o);
 }
 setnum(state,"next_schedule_id",next);return out;
fail:cJSON_Delete(out);return NULL;
}
static char *cron_block(void) {
 char *s=calloc(32768,1);size_t used=0;used+=(size_t)snprintf(s,32768,"%s\n# Robot local time; the system crond timezone must match.\n",BEGIN);
 cJSON *j;cJSON_ArrayForEach(j,get(state,"schedules"))if(cJSON_IsTrue(get(j,"enabled"))){
  const char *t=str(j,"time","");char days[20]="";size_t n=0;
  for(int d=0;d<7;d++)if(has_day(j,d))n+=(size_t)snprintf(days+n,sizeof(days)-n,"%s%d",n?",":"",d);
  if(strcmp(mode,"demo")&&schedule_extra(j)){
   char command[1200],quoted[1030];const char *action=str(j,"action","");
   if(!strcmp(action,"random_song"))snprintf(command,sizeof command,"%s",SCHEDULE_RANDOM);
   else if(!strcmp(action,"play_song"))snprintf(command,sizeof command,"%s %s/%s",SCHEDULE_PLAYER,SCHEDULE_SONGS,str(j,"song",""));
   else {schedule_quote(quoted,str(j,"command",""));snprintf(command,sizeof command,"/bin/sh -c %s",quoted);}
   used+=(size_t)snprintf(s+used,32768-used,"%d %d * * %s %s >/dev/null 2>&1\n",atoi(t+3),atoi(t),days,command);continue;
  }
  if(direct_control(j)){
   char power[8]="";if(!strcmp(str(j,"action","clean"),"clean")&&get(j,"fan_power"))snprintf(power,sizeof power," %d",fan_power(j));
   used+=(size_t)snprintf(s+used,32768-used,"%d %d * * %s %s %s%s >/dev/null 2>&1\n",atoi(t+3),atoi(t),days,control_bin,str(j,"action","clean"),power);
   continue;
  }
  used+=(size_t)snprintf(s+used,32768-used,"%d %d * * %s '%s' --data-dir '%s' --port %d --listen %s --run-schedule %d >/dev/null 2>&1\n",atoi(t+3),atoi(t),days,executable,data_dir,port,bind_ip,num(j,"id",0));
 }
 snprintf(s+used,32768-used,"%s\n",END);return s;
}
static char *merge_cron(const char *old,const char *block) {
 const char *a=strstr(old,BEGIN),*b=strstr(old,END);
 if((a&&!b)||(!a&&b)|| (a&&b<a))return NULL;
 if(a&&(strstr(a+strlen(BEGIN),BEGIN)||strstr(b+strlen(END),END)))return NULL;
 if(a&&((a!=old&&a[-1]!='\n')||(a[strlen(BEGIN)]!='\n')))return NULL;
 if(b&&((b!=old&&b[-1]!='\n')||(b[strlen(END)]&&b[strlen(END)]!='\n')))return NULL;
 size_t prefix=a?(size_t)(a-old):strlen(old);const char *suffix=b?b+strlen(END):"";if(*suffix=='\n')suffix++;
 size_t len=prefix+strlen(block)+strlen(suffix)+3;if(len>=LIMIT)return NULL;char *out=calloc(len,1);
 memcpy(out,old,prefix);if(prefix&&out[prefix-1]!='\n')out[prefix++]='\n';strcat(out,block);strcat(out,suffix);return out;
}
static bool sync_cron(char *error,size_t cap) {
 char *block=cron_block(),p[1024];path(p,sizeof(p),"cron.preview");
 if(!atomic_write(p,block)){free(block);snprintf(error,cap,"No se pudo guardar el crontab");return false;}
 if(!cron_dir){free(block);return true;}
 cJSON *health=cron_health(true);bool ready=cJSON_IsTrue(get(health,"available"))&&cJSON_IsTrue(get(health,"running"));cJSON_Delete(health);
 if(!ready){free(block);snprintf(error,cap,"Cron is unavailable or not running with the configured directory");return false;}
 char *old=read_cron_table(error,cap);if(!old){free(block);return false;}
 char *readargv[]={(char*)cron_bin,"-c",(char*)cron_dir,"-l",NULL};
 char *merged=merge_cron(old,block);free(block);free(old);
 if(!merged){snprintf(error,cap,"Bloque de crontab existente ambiguo; no se modifica");return false;}
 path(p,sizeof(p),"cron.pending");bool ok=atomic_write(p,merged);free(merged);
 if(ok){char *argv[]={(char*)cron_bin,"-c",(char*)cron_dir,p,NULL};ok=run_process(argv,NULL,0)==0;}unlink(p);
 if(ok){char verify[LIMIT]={0};ok=run_process(readargv,verify,sizeof(verify))==0;char *a=strstr(verify,BEGIN),*b=a?strstr(a,END):NULL;char *expected=cron_block();size_t len=strlen(expected);ok=ok&&a&&b&&!strncmp(a,expected,len);free(expected);}
 if(!ok)snprintf(error,cap,"Crontab installation could not be verified");
 return ok;
}
static void demo_tick(void) {
 if(strcmp(mode,"demo"))return;
 if(!strcmp(activity,"returning")&&time(NULL)-action_at>8){strcpy(activity,"charging");event("state","At the dock · simulation");save_state();}
}
static bool reset_waiting(void){cJSON *p=get(state,"map_reset_pending");const char *phase=str(p,"phase","");return cJSON_IsObject(p)&&strcmp(phase,"not_sent")&&strcmp(phase,"new_map_observed");}
static bool update_map_reset(void);
#include "zigma_command_feedback.h"
#include "zigma_wifi.h"
static cJSON *status_json(void) {
 update_map_reset();
 demo_tick(); cJSON *o=NULL;
 if(status_file){char *s=read_file(status_file,65536);if(s){o=strict_json(s);free(s);}}
 if(!cJSON_IsObject(o)){cJSON_Delete(o);o=cJSON_CreateObject();}
 cJSON_AddStringToObject(o,"mode",mode);cJSON_AddStringToObject(o,"robot",str(state,"robot_name",robot));cJSON_AddNumberToObject(o,"server_time",time(NULL));cJSON_AddStringToObject(o,"timezone",current_zone);cJSON_AddStringToObject(o,"area_unit",str(state,"area_unit","m2"));
 if(!strcmp(mode,"demo")){
  setstr(o,"working_status",activity);setnum(o,"battery_level",87);setstr(o,"error","NoError");setbool(o,"fresh",true);
  int elapsed=!strcmp(activity,"cleaning")?(int)(time(NULL)-action_at):num(state,"demo_elapsed",0);setnum(o,"clean_time_s",elapsed);setnum(o,"clean_area_m2",elapsed/18.0);setstr(o,"source","synthetic");setnum(o,"observed_at",time(NULL));
 }else{
  double ts=cJSON_IsNumber(get(o,"observed_at"))?get(o,"observed_at")->valuedouble:0;
  struct timespec current;clock_gettime(CLOCK_REALTIME,&current);
  double age=(double)current.tv_sec+current.tv_nsec/1e9-ts;
  setbool(o,"fresh",ts>0&&age>=-1&&age<=30);
  setstr(o,"telemetry_state",ts<=0?"missing":age < -1?"clock_mismatch":age>30?"stale":"fresh");
  double cache_age=(current.tv_sec+current.tv_nsec/1e9)-(cJSON_IsNumber(get(o,"cache_read_at"))?get(o,"cache_read_at")->valuedouble:0);
  bool cache_available=cache_age>=0&&cache_age<=15;
  setbool(o,"cache_available",cache_available);
  if(cache_available&&!cJSON_IsTrue(get(o,"fresh"))&&age>=-1)setstr(o,"telemetry_state","cached");
  double link_age=(current.tv_sec+current.tv_nsec/1e9)-(cJSON_IsNumber(get(o,"local_link_received_at"))?get(o,"local_link_received_at")->valuedouble:0);
  setbool(o,"local_link_active",cache_available&&link_age>=0&&link_age<=15);
  if(ts>0&&age>=0)setnum(o,"observation_age_s",floor(age));else cJSON_AddNullToObject(o,"observation_age_s");
  setbool(o,"statistics_available",cJSON_IsNumber(get(o,"clean_time_s"))&&cJSON_IsNumber(get(o,"clean_area_m2")));
  cJSON *pose=get(o,"navigation_pose"),*read_at=get(pose,"cache_read_at");double pose_age=cJSON_IsNumber(read_at)?current.tv_sec+current.tv_nsec/1e9-read_at->valuedouble:-1;
  if(pose_age<0||pose_age>15)cJSON_DeleteItemFromObjectCaseSensitive(o,"navigation_pose");
  if(!get(o,"working_status"))setstr(o,"working_status","unknown");
  setstr(o,"source","external_status_file");
 }
 cJSON *caps=cJSON_CreateObject();cJSON_AddBoolToObject(caps,"basic_control",!strcmp(mode,"demo")||control_bin);cJSON_AddBoolToObject(caps,"map_edit",!reset_waiting()&&(!strcmp(mode,"demo")||zones_bin));cJSON_AddBoolToObject(caps,"area_clean",!reset_waiting()&&(!strcmp(mode,"demo")||area_bin));cJSON_AddItemToObject(o,"capabilities",caps);
 setbool(o,"map_reset_pending",reset_waiting());cJSON *transition=get(state,"map_reset_pending");if(transition&&!strcmp(str(transition,"operation",""),"restore"))cJSON_AddItemToObject(o,"map_restore",cJSON_Duplicate(transition,1));
 if(reset_waiting()||time(NULL)-num(transition,"completed_at",0)<15)cJSON_DeleteItemFromObjectCaseSensitive(o,"navigation_pose");
 cJSON *geometry=get(o,"map_geometry");double geometry_age=time(NULL)-num(geometry,"cache_read_at",0);
 if(reset_waiting()||geometry_age<0||geometry_age>15||num(geometry,"received_at",0)<=num(transition,"at",0))cJSON_DeleteItemFromObjectCaseSensitive(o,"map_geometry");
 cJSON_AddBoolToObject(o,"cron_installed",cron_dir&&cJSON_IsTrue(get(state,"cron_applied")));cf_view(o);wifi_status(o);return o;
}
static bool rect_valid(cJSON *r) {
 if(!cJSON_IsObject(r))return false;
 const char *keys[]={"x","y","width","height"};double v[4];
 for(int i=0;i<4;i++){cJSON *n=get(r,keys[i]);if(!cJSON_IsNumber(n)||!isfinite(n->valuedouble))return false;v[i]=n->valuedouble;}
 return v[0]>=0&&v[1]>=0&&v[2]>=2&&v[3]>=2&&v[0]+v[2]<=120&&v[1]+v[3]<=90;
}
static bool issue_area(cJSON *body,char *err,size_t cap){
 cJSON *map=NULL,*st=NULL;bool ok=false;char *raw=map_file?read_file(map_file,2*1024*1024):NULL;
 if(raw){map=strict_json(raw);free(raw);}cJSON *r=get(body,"rect"),*context=get(body,"map_context"),*origin=get(map,"map_origin"),*wanted=get(context,"map_origin");
 if(!cJSON_IsTrue(get(map,"available"))||!cJSON_IsObject(context)||!cJSON_IsString(get(map,"map_id"))||!strlen(str(map,"map_id",""))||strcmp(str(map,"map_id",""),str(context,"map_id",""))||!cJSON_IsNumber(get(context,"width"))||!cJSON_IsNumber(get(context,"height"))||num(map,"width",0)!=get(context,"width")->valuedouble||num(map,"height",0)!=get(context,"height")->valuedouble||cJSON_GetArraySize(origin)!=2||cJSON_GetArraySize(wanted)!=2){snprintf(err,cap,"The map changed. Draw the area again");goto done;}
 double xy[2];for(int i=0;i<2;i++){cJSON *a=cJSON_GetArrayItem(origin,i),*b=cJSON_GetArrayItem(wanted,i);if(!cJSON_IsNumber(a)||!cJSON_IsNumber(b)||!isfinite(a->valuedouble)||a->valuedouble!=b->valuedouble||floor(a->valuedouble)!=a->valuedouble||fabs(a->valuedouble)>20000){snprintf(err,cap,"The map origin changed or is unsupported");goto done;}xy[i]=a->valuedouble;}
 double v[4];const char *keys[]={"x","y","width","height"};for(int i=0;i<4;i++){cJSON *n=get(r,keys[i]);if(!cJSON_IsNumber(n)||!isfinite(n->valuedouble)||floor(n->valuedouble)!=n->valuedouble){snprintf(err,cap,"Rectangle coordinates must be whole grid cells");goto done;}v[i]=n->valuedouble;}
 if(v[0]<0||v[1]<0||v[2]<2||v[3]<2||v[0]+v[2]>num(map,"width",0)||v[1]+v[3]>num(map,"height",0)){snprintf(err,cap,"Select a valid rectangle inside the map");goto done;}
 double native[4]={v[0]-xy[0],v[1]-xy[1],v[0]+v[2]-xy[0],v[1]+v[3]-xy[1]};for(int i=0;i<4;i++)if(fabs(native[i])>20000){snprintf(err,cap,"Rectangle exceeds local protocol limits");goto done;}
 st=status_json();const char *work=str(st,"working_status","unknown");bool available=cJSON_IsTrue(get(st,"fresh"))||(cJSON_IsTrue(get(st,"cache_available"))&&cJSON_IsTrue(get(st,"local_link_active")));
 if(!available||strcmp(str(st,"error","unknown"),"NoError")||(strcmp(work,"PileCharging")&&strcmp(work,"DirCharging")&&strcmp(work,"Standby")&&strcmp(work,"Pause")&&strcmp(work,"charging")&&strcmp(work,"idle")&&strcmp(work,"paused"))){snprintf(err,cap,"Area cleaning requires an idle robot with confirmed local communication and no error");goto done;}
 char values[4][24];for(int i=0;i<4;i++)snprintf(values[i],sizeof(values[i]),"%.0f",native[i]);char *args[]={(char*)area_bin,values[0],values[1],values[2],values[3],NULL};
 if(run_process(args,NULL,0)){snprintf(err,cap,"Area command failed or timed out; check the robot before retrying");goto done;}
 event("command","Area command dispatched; waiting for robot feedback");ok=true;
 done:cJSON_Delete(st);cJSON_Delete(map);return ok;
}
static bool zone_map_context(cJSON *map,cJSON *context){
 if(!cJSON_IsTrue(get(map,"available"))||!cJSON_IsTrue(get(map,"zones_available"))||!cJSON_IsObject(context)||!*str(map,"map_id","")||strcmp(str(map,"map_id",""),str(context,"map_id","")))return false;
 const char *keys[]={"width","height"};for(int i=0;i<2;i++)if(!cJSON_IsNumber(get(context,keys[i]))||!cJSON_Compare(get(map,keys[i]),get(context,keys[i]),1))return false;
 return cJSON_IsArray(get(map,"map_origin"))&&cJSON_GetArraySize(get(map,"map_origin"))==2&&cJSON_Compare(get(map,"map_origin"),get(context,"map_origin"),1);
}
static cJSON *zone_from_rect(cJSON *r,cJSON *flags,cJSON *map){
 double v[4];const char *keys[]={"x","y","width","height"};for(int i=0;i<4;i++){cJSON *n=get(r,keys[i]);if(!cJSON_IsNumber(n)||!isfinite(n->valuedouble)||fabs(n->valuedouble-round(n->valuedouble))>0.00001)return NULL;v[i]=round(n->valuedouble);}
 if(v[0]<0||v[1]<0||v[2]<2||v[3]<2||v[0]+v[2]>num(map,"width",0)||v[1]+v[3]>num(map,"height",0)||!cJSON_IsBool(get(flags,"can_clean"))||!cJSON_IsBool(get(flags,"can_mop"))||(cJSON_IsTrue(get(flags,"can_clean"))&&cJSON_IsTrue(get(flags,"can_mop"))))return NULL;
 cJSON *z=cJSON_CreateObject(),*points=cJSON_CreateArray();for(int i=0;i<4;i++){cJSON *p=cJSON_CreateObject();setnum(p,"x",v[0]+(i>=2?v[2]:0));setnum(p,"y",v[1]+(i==1||i==2?v[3]:0));cJSON_AddItemToArray(points,p);}cJSON_AddItemToObject(z,"points",points);cJSON_AddBoolToObject(z,"can_clean",cJSON_IsTrue(get(flags,"can_clean")));cJSON_AddBoolToObject(z,"can_mop",cJSON_IsTrue(get(flags,"can_mop")));return z;
}
static bool same_zone_point(cJSON *a,cJSON *b){const char *keys[]={"x","y"};for(int i=0;i<2;i++){cJSON *x=get(a,keys[i]),*y=get(b,keys[i]);if(!cJSON_IsNumber(x)||!cJSON_IsNumber(y)||!isfinite(x->valuedouble)||!isfinite(y->valuedouble)||fabs(x->valuedouble-y->valuedouble)>0.00001)return false;}return true;}
static bool same_polygon(cJSON *a,cJSON *b){
 if(!cJSON_IsArray(a)||!cJSON_IsArray(b)||cJSON_GetArraySize(a)!=4||cJSON_GetArraySize(b)!=4)return false;
 for(int direction=-1;direction<=1;direction+=2)for(int offset=0;offset<4;offset++){bool equal=true;for(int i=0;i<4;i++)if(!same_zone_point(cJSON_GetArrayItem(a,i),cJSON_GetArrayItem(b,(offset+direction*i+4)%4))){equal=false;break;}if(equal)return true;}
 return false;
}
static bool same_zones(cJSON *a,cJSON *b){if(!cJSON_IsArray(a)||!cJSON_IsArray(b)||cJSON_GetArraySize(a)!=cJSON_GetArraySize(b))return false;for(int i=0;i<cJSON_GetArraySize(a);i++){cJSON *x=cJSON_GetArrayItem(a,i),*y=cJSON_GetArrayItem(b,i);if(!same_polygon(get(x,"points"),get(y,"points")))return false;const char *keys[]={"can_clean","can_mop"};for(int k=0;k<2;k++)if(!cJSON_Compare(get(x,keys[k]),get(y,keys[k]),1))return false;}return true;}

static bool issue_zones(cJSON *body,char *err,size_t cap){
 static time_t pending_until=0;static cJSON *pending_target=NULL;
 cJSON *map=NULL,*st=NULL,*target=NULL,*wire=NULL;char *raw=read_file(map_file,2*1024*1024),*payload=NULL;bool ok=false;
 if(raw){map=strict_json(raw);free(raw);}snprintf(err,cap,"Map or zones changed. Reload before editing");
 if(!zone_map_context(map,get(body,"map_context"))||!cJSON_IsArray(get(map,"zones"))||!cJSON_Compare(get(map,"zones"),get(body,"base_zones"),1))goto done;
 if(pending_target&&same_zones(get(map,"zones"),pending_target))pending_until=0;
 if(time(NULL)<pending_until){snprintf(err,cap,"Waiting for the previous zone update. Review the map before retrying");goto done;}
 st=status_json();const char *work=str(st,"working_status","");bool ready=cJSON_IsTrue(get(st,"fresh"))||(cJSON_IsTrue(get(st,"cache_available"))&&cJSON_IsTrue(get(st,"local_link_active")));
 if(!ready||strcmp(str(st,"error","unknown"),"NoError")||(strcmp(work,"charging")&&strcmp(work,"PileCharging")&&strcmp(work,"DirCharging")&&strcmp(work,"Standby")&&strcmp(work,"idle"))){snprintf(err,cap,"Zone editing requires an idle robot with confirmed local communication and no error");goto done;}
 const char *op=str(body,"operation","");int count=cJSON_GetArraySize(get(map,"zones")),index=num(body,"index",-1);
 if(strcmp(op,"add")&&strcmp(op,"update")&&strcmp(op,"delete")){snprintf(err,cap,"Unknown zone operation");goto done;}
 if(strcmp(op,"add")&&(!cJSON_IsNumber(get(body,"index"))||get(body,"index")->valuedouble!=index||index<0||index>=count)){snprintf(err,cap,"Select an existing zone");goto done;}
 if(count>12||(!strcmp(op,"add")&&count>=12)){snprintf(err,cap,"Maximum 12 zones");goto done;}
 target=cJSON_Duplicate(get(map,"zones"),1);if(!strcmp(op,"delete"))cJSON_DeleteItemFromArray(target,index);else{cJSON *z=zone_from_rect(get(body,"rect"),get(body,"flags"),map);if(!z){snprintf(err,cap,"Select a valid rectangle and restriction type");goto done;}if(!strcmp(op,"add"))cJSON_AddItemToArray(target,z);else cJSON_ReplaceItemInArray(target,index,z);}
 wire=cJSON_CreateObject();cJSON_AddStringToObject(wire,"ProtocolVersion","1.0");cJSON_AddStringToObject(wire,"cmd_origin","app");cJSON *zones=cJSON_AddArrayToObject(wire,"forbidden_zone_info"),*z;
 cJSON *origin=get(map,"map_origin");double ox=cJSON_GetArrayItem(origin,0)->valuedouble,oy=cJSON_GetArrayItem(origin,1)->valuedouble;
 cJSON_ArrayForEach(z,target){cJSON *points=get(z,"points");if(!cJSON_IsArray(points)||cJSON_GetArraySize(points)!=4||!cJSON_IsBool(get(z,"can_clean"))||!cJSON_IsBool(get(z,"can_mop"))){snprintf(err,cap,"Existing zones have an unsupported format; nothing sent");goto done;}cJSON *w=cJSON_CreateObject(),*xs=cJSON_AddArrayToObject(w,"x"),*ys=cJSON_AddArrayToObject(w,"y");cJSON_AddItemToArray(zones,w);
  for(int i=0;i<4;i++){cJSON *p=cJSON_GetArrayItem(points,i),*x=get(p,"x"),*y=get(p,"y");if(!cJSON_IsNumber(x)||!cJSON_IsNumber(y)){snprintf(err,cap,"Invalid zone point");goto done;}double a=x->valuedouble-ox,b=y->valuedouble-oy;if(!isfinite(a)||!isfinite(b)||fabs(a)>20000||fabs(b)>20000||fabs(a-round(a))>0.00001||fabs(b-round(b))>0.00001){snprintf(err,cap,"Zone coordinates cannot be sent without loss; nothing changed");goto done;}cJSON_AddItemToArray(xs,cJSON_CreateNumber(round(a)));cJSON_AddItemToArray(ys,cJSON_CreateNumber(round(b)));}
  cJSON_AddBoolToObject(w,"can_clean",cJSON_IsTrue(get(z,"can_clean")));cJSON_AddBoolToObject(w,"can_mop",cJSON_IsTrue(get(z,"can_mop")));}
 payload=cJSON_PrintUnformatted(wire);if(!payload||strlen(payload)>=4096){snprintf(err,cap,"Zone message exceeds protocol size");goto done;}
 char backup[1024],name[100];snprintf(name,sizeof(name),"zones-before-%ld-%d.json",(long)time(NULL),num(state,"sequence",0));path(backup,sizeof(backup),name);cJSON *prior=cJSON_CreateObject();cJSON_AddItemToObject(prior,"map_context",cJSON_Duplicate(get(body,"map_context"),1));cJSON_AddItemToObject(prior,"zones",cJSON_Duplicate(get(map,"zones"),1));char *snapshot=cJSON_PrintUnformatted(prior);cJSON_Delete(prior);bool saved=snapshot&&atomic_write(backup,snapshot);free(snapshot);if(!saved){snprintf(err,cap,"Could not back up current zones; nothing sent");goto done;}
 char *args[]={(char*)zones_bin,payload,NULL};pending_until=time(NULL)+30;
 if(run_process(args,NULL,0)){snprintf(err,cap,"Zone dispatch failed or timed out. Reload before retrying");goto done;}
 cJSON_Delete(pending_target);pending_target=cJSON_Duplicate(target,1);event("map","Zone update dispatched; waiting for robot map feedback");ok=true;
 done:free(payload);cJSON_Delete(map);cJSON_Delete(st);cJSON_Delete(target);cJSON_Delete(wire);return ok;
}

static bool issue(const char *action,cJSON *body,char *err,size_t cap) {
 bool cleaning=!strcmp(action,"clean")||!strcmp(action,"resume");
 int power=fan_power(body);if(cleaning&&!power){snprintf(err,cap,"Fan power must be 1, 2 or 3");return false;}
 if(reset_waiting()&&(!strcmp(action,"area_clean")||!strcmp(action,"set_forbidden"))){snprintf(err,cap,"Waiting for a new map after clear request; geometry commands are blocked");return false;}
 bool basic=!strcmp(action,"clean")||!strcmp(action,"pause")||!strcmp(action,"resume")||!strcmp(action,"dock");
 if(!basic&&strcmp(action,"area_clean")&&strcmp(action,"set_forbidden")){snprintf(err,cap,"Unsupported command");return false;}
 if(strcmp(mode,"demo")){
  if(!strcmp(action,"area_clean")&&area_bin)return issue_area(body,err,cap);
  if(!strcmp(action,"set_forbidden")&&zones_bin)return issue_zones(body,err,cap);
  if(!basic||!control_bin){snprintf(err,cap,"This feature has no real adapter yet");return false;}
  cJSON *baseline=status_json();char level[2]={(char)('0'+power),0};char *argv[]={(char*)control_bin,(char*)action,cleaning?level:NULL,NULL};if(run_process(argv,NULL,0)){cJSON_Delete(baseline);snprintf(err,cap,"Local control failed or timed out");return false;}
  cf_begin(action,baseline);cJSON_Delete(baseline);
  event("command","Command dispatched to local control; waiting for robot feedback");return true;
 }
 if(!strcmp(action,"area_clean")&&!rect_valid(get(body,"rect"))){snprintf(err,cap,"Select a valid rectangle inside the map");return false;}
 if(!strcmp(action,"set_forbidden")&&cJSON_IsString(get(body,"operation"))){
  const char *op=str(body,"operation","");int index=num(body,"index",-1),count=cJSON_GetArraySize(get(state,"zones"));cJSON *target=cJSON_Duplicate(get(state,"zones"),1),*m=cJSON_CreateObject();setnum(m,"width",120);setnum(m,"height",90);cJSON *z=NULL;bool valid=true;
  if(strcmp(op,"add")&&(index<0||index>=count||!cJSON_IsNumber(get(body,"index"))||get(body,"index")->valuedouble!=index))valid=false;
  if(!strcmp(op,"delete")){if(valid)cJSON_DeleteItemFromArray(target,index);}else if(!strcmp(op,"add")||!strcmp(op,"update")){z=zone_from_rect(get(body,"rect"),get(body,"flags"),m);if(!z||(!strcmp(op,"add")&&count>=12))valid=false;if(valid){if(!strcmp(op,"add"))cJSON_AddItemToArray(target,z);else cJSON_ReplaceItemInArray(target,index,z);z=NULL;}}else valid=false;
  cJSON_Delete(z);cJSON_Delete(m);if(!valid){cJSON_Delete(target);snprintf(err,cap,"Invalid zone operation");return false;}cJSON_ReplaceItemInObjectCaseSensitive(state,"zones",target);event("map","No-go zones saved · simulation");return true;
 }
 if(!strcmp(action,"set_forbidden")){
  cJSON *zs=get(body,"zones"),*z;if(!cJSON_IsArray(zs)||cJSON_GetArraySize(zs)>12){snprintf(err,cap,"Maximum 12 zones");return false;}
  cJSON_ArrayForEach(z,zs)if(!rect_valid(z)){snprintf(err,cap,"Invalid zone");return false;}
  cJSON_ReplaceItemInObjectCaseSensitive(state,"zones",cJSON_Duplicate(zs,1));event("map","No-go zones saved · simulation");return true;
 }
 if(!strcmp(action,"pause")){if(!strcmp(activity,"cleaning"))setnum(state,"demo_elapsed",time(NULL)-action_at);strcpy(activity,"paused");}
 else if(!strcmp(action,"dock")){strcpy(activity,"returning");action_at=time(NULL);}
 else {strcpy(activity,"cleaning");action_at=time(NULL)-(!strcmp(action,"resume")?num(state,"demo_elapsed",0):0);if(strcmp(action,"resume"))setnum(state,"demo_elapsed",0);}
 const char *label=!strcmp(action,"pause")?"Pause":!strcmp(action,"dock")?"Returning to dock":!strcmp(action,"resume")?"Cleaning resumed":!strcmp(action,"area_clean")?"Area cleaning":"Cleaning started";
 char msg[160];snprintf(msg,sizeof(msg),"%s · simulation",label);event("command",msg);return true;
}
static cJSON *demo_map(void) {
 if(map_file){char *s=read_file(map_file,2*1024*1024);if(s){cJSON *o=strict_json(s);free(s);if(o){setstr(o,"source","external_map_file");cJSON *estimate=get(o,"position_estimate");struct timespec estimate_now;clock_gettime(CLOCK_REALTIME,&estimate_now);double age=estimate_now.tv_sec+estimate_now.tv_nsec/1e9-(cJSON_IsNumber(get(estimate,"received_at"))?get(estimate,"received_at")->valuedouble:0);if(age<0||age>6)cJSON_DeleteItemFromObjectCaseSensitive(o,"position_estimate");return o;}}}
 if(strcmp(mode,"demo")){cJSON *o=cJSON_CreateObject();cJSON_AddBoolToObject(o,"available",false);return o;}
 cJSON *o=cJSON_CreateObject(),*cells=cJSON_CreateArray();int w=120,h=90;
 for(int y=0;y<h;y++)for(int x=0;x<w;x++){
  int v=3;bool in=(x>=9&&x<=108&&y>=10&&y<=77);
  if(in){v=0;if(x==9||x==108||y==10||y==77)v=1;
   if((x==53&&y>=10&&y<=60&&!(y>=34&&y<=43))||(y==51&&x>=53&&x<=108&&!(x>=77&&x<=87)))v=1;
   if((x>=25&&x<=32&&y>=25&&y<=31)||(x>=76&&x<=91&&y>=63&&y<=68))v=1;}
  cJSON_AddItemToArray(cells,cJSON_CreateNumber(v));
 }
 cJSON_AddNumberToObject(o,"width",w);cJSON_AddNumberToObject(o,"height",h);cJSON_AddNumberToObject(o,"resolution",0.05);cJSON_AddItemToObject(o,"cells",cells);cJSON_AddStringToObject(o,"map_id","synthetic-home-v1");cJSON_AddStringToObject(o,"source","synthetic");cJSON_AddBoolToObject(o,"available",true);
 cJSON *dock=cJSON_CreateObject();cJSON_AddNumberToObject(dock,"x",87);cJSON_AddNumberToObject(dock,"y",20);cJSON_AddItemToObject(o,"dock",dock);
 cJSON *pts=cJSON_CreateArray();double anchors[][2]={{87,23},{75,24},{61,36},{48,38},{39,47},{24,47},{23,57},{42,58},{42,64},{22,64}};
 for(size_t i=0;i<sizeof(anchors)/sizeof(anchors[0]);i++){cJSON *p=cJSON_CreateObject();cJSON_AddNumberToObject(p,"x",anchors[i][0]);cJSON_AddNumberToObject(p,"y",anchors[i][1]);cJSON_AddItemToArray(pts,p);}cJSON_AddItemToObject(o,"path",pts);
 cJSON *pos=cJSON_CreateObject();double t=fmod((!strcmp(activity,"cleaning")?(double)(time(NULL)-action_at):num(state,"demo_elapsed",0))/5.0,9);int index=(int)t;if(!strcmp(activity,"returning")||!strcmp(activity,"charging"))index=0;
 cJSON_AddNumberToObject(pos,"x",anchors[index][0]);cJSON_AddNumberToObject(pos,"y",anchors[index][1]);cJSON_AddNullToObject(pos,"heading");cJSON_AddStringToObject(pos,"kind","simulated");cJSON_AddItemToObject(o,"position",pos);
 cJSON_AddItemToObject(o,"zones",cJSON_Duplicate(get(state,"zones"),1));return o;
}
static double next_occurrence(cJSON *job,time_t now) {
 if(!cJSON_IsTrue(get(job,"enabled")))return 0;
 for(int step=1;step<=8*24*60;step++){
  time_t t=(now/60+step)*60;struct tm tm;localtime_r(&t,&tm);char hh[6],date[16];
  strftime(hh,sizeof(hh),"%H:%M",&tm);strftime(date,sizeof(date),"%Y-%m-%d",&tm);
  if(has_day(job,tm.tm_wday)&&!strcmp(hh,str(job,"time",""))&&(direct_control(job)||(t>num(job,"last_run_at",0)&&strcmp(date,str(job,"last_started_date","")))))return (double)t;
 }
 return 0;
}
static cJSON *schedule_json(void) {
 cJSON *o=cJSON_CreateObject(),*items=cJSON_Duplicate(get(state,"schedules"),1),*j;time_t now=time(NULL);
 cJSON_ArrayForEach(j,items){
  double next=next_occurrence(j,now);if(next)cJSON_AddNumberToObject(j,"next_run",next);else cJSON_AddNullToObject(j,"next_run");
  bool direct=direct_control(j);setstr(j,"execution_method",direct?"direct_control":"agent");
  if(direct){const char *keys[]={"last_result","last_started_date","last_run_at","last_state_check",NULL};for(int i=0;keys[i];i++)cJSON_DeleteItemFromObjectCaseSensitive(j,keys[i]);setstr(j,"execution_note","Direct cron command; execution is not confirmed by the web");}
 }
 cJSON_AddItemToObject(o,"items",items);cJSON_AddItemToObject(o,"songs",schedule_song_list());cJSON_AddStringToObject(o,"timezone",current_zone);char *cron=cron_block();cJSON_AddStringToObject(o,"cron_preview",cron);
 char error[200]={0};char *raw=read_cron_table(error,sizeof(error));bool view_valid=false,truncated=false,matching=false;
 if(raw){
  setstr(o,"crontab_raw",raw);cJSON *external=external_cron_entries(raw,&view_valid,&truncated);if(external)cJSON_AddItemToObject(o,"external_entries",external);
  char *start=strstr(raw,BEGIN);matching=view_valid&&start&&!strncmp(start,cron,strlen(cron));
  if(!view_valid)snprintf(error,sizeof(error),"Managed cron block is ambiguous; inspect the raw table");
 }
 setbool(o,"external_available",raw&&view_valid);setbool(o,"external_truncated",truncated);if(*error)setstr(o,"external_error",error);free(raw);free(cron);
 cJSON *health=cron_health(false);bool healthy=cJSON_IsTrue(get(health,"available"))&&cJSON_IsTrue(get(health,"running"));
 cJSON_AddBoolToObject(o,"installed",cron_dir&&cJSON_IsTrue(get(state,"cron_applied"))&&healthy&&matching);if(health)cJSON_AddItemToObject(o,"cron",health);
 cJSON_AddStringToObject(o,"clock_policy","Robot local time. Full clean and reboot run directly from cron. Dock and restore use guarded agent actions.");
 cJSON_AddStringToObject(o,"backend","crontab");return o;
}
static void send_all(int fd,const char *p,size_t n) {while(n){ssize_t s=send(fd,p,n,MSG_NOSIGNAL);if(s<=0)return;p+=s;n-=(size_t)s;}}
static void response(int fd,int code,const char *type,const char *s,size_t n) {
 char head[1024];snprintf(head,sizeof(head),"HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nContent-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; media-src 'self' blob:; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'\r\n\r\n",code,code<300?"OK":"Error",type,n);send_all(fd,head,strlen(head));send_all(fd,s,n);
}
static void json_response(int fd,int code,cJSON *o){char *s=cJSON_PrintUnformatted(o);if(s)response(fd,code,"application/json; charset=utf-8",s,strlen(s));free(s);cJSON_Delete(o);}
static void error_response(int fd,int code,const char *message){cJSON *o=cJSON_CreateObject();cJSON_AddStringToObject(o,"error",message);json_response(fd,code,o);}
static bool header_value(const char *req,const char *name,char *out,size_t cap){
 const char *p=strstr(req,"\r\n");if(!p)return false;p+=2;bool found=false;
 while(*p&&strncmp(p,"\r\n",2)){const char *e=strstr(p,"\r\n"),*colon=strchr(p,':');if(!e)return false;
  if(colon&&colon<e&&(size_t)(colon-p)==strlen(name)&&!strncasecmp(p,name,strlen(name))){if(found)return false;const char *v=colon+1;while(v<e&&*v==' ')v++;if((size_t)(e-v)>=cap)return false;memcpy(out,v,(size_t)(e-v));out[e-v]=0;found=true;}p=e+2;}
 return found;
}
static bool allowed_host(const char *host){
 char hostname[128];snprintf(hostname,sizeof(hostname),"%s",host);char *c=strrchr(hostname,':');if(!c)return false;*c++=0;char expected[16];snprintf(expected,sizeof(expected),"%d",port);if(strcmp(c,expected))return false;
 struct in_addr ip;char local[128]={0};gethostname(local,sizeof(local)-1);return !strcmp(hostname,"localhost")||!strcasecmp(hostname,local)||inet_pton(AF_INET,hostname,&ip)==1;
}
static bool authorized(const char *req){
 if(!strcmp(mode,"demo")&&!strcmp(bind_ip,"127.0.0.1"))return true;
 char auth[180],want[180];snprintf(want,sizeof(want),"Bearer %s",token);
 if(!header_value(req,"Authorization",auth,sizeof(auth)))return false;
 unsigned diff=(unsigned)(strlen(auth)^strlen(want));size_t n=strlen(want);for(size_t i=0;i<n;i++)diff|=(unsigned)((i<strlen(auth)?auth[i]:0)^want[i]);return diff==0;
}
static bool dockable_state(const char *work){
 const char *states[]={"AutoClean","PlanningRect","SpotClean","SelectClean","Pause","BackCharging","cleaning","paused","returning",NULL};
 for(int i=0;states[i];i++)if(!strcmp(work,states[i]))return true;
 return false;
}
static void scheduled_run(int fd,int id){
 cJSON *j=NULL,*v;cJSON_ArrayForEach(v,get(state,"schedules"))if(num(v,"id",0)==id)j=v;
 if(!j){error_response(fd,404,"Schedule not found");return;}
 struct tm tm;time_t now;now_local(&tm,&now);char hh[6],date[16];strftime(hh,sizeof(hh),"%H:%M",&tm);strftime(date,sizeof(date),"%Y-%m-%d",&tm);
 if(!cJSON_IsTrue(get(j,"enabled"))||!has_day(j,tm.tm_wday)||strcmp(hh,str(j,"time",""))){error_response(fd,409,"Schedule disabled or outside its due minute");return;}
 if(!strcmp(str(j,"last_started_date",""),date)){error_response(fd,409,"This schedule has already been processed today");return;}
 if(now<num(j,"last_run_at",0)){error_response(fd,409,"Clock moved backwards; schedule not repeated");return;}
 if(now<1767225600){error_response(fd,409,"Clock unconfirmed; schedule not executed");return;}
 if(cron_dir&&!cJSON_IsTrue(get(state,"cron_applied"))){error_response(fd,409,"Crontab installation pending");return;}
 // Consume the local date before every attempt, including skips and failures.
 setstr(j,"last_started_date",date);setnum(j,"last_run_at",now);setstr(j,"last_result","attempt_recorded");
 if(!save_state()){error_response(fd,500,"Cannot record execution; no command sent");return;}
 const char *action=str(j,"action","clean");bool restoring=!strcmp(action,"restore_master");
 cJSON *st=status_json();const char *work=str(st,"working_status","unknown");bool blocked=reset_waiting()&&(!restoring||!strcmp(str(get(state,"map_reset_pending"),"operation",""),"restore"));
 bool idle=!strcmp(work,"charging")||!strcmp(work,"PileCharging")||!strcmp(work,"DirCharging")||!strcmp(work,"Standby")||!strcmp(work,"idle");
 bool fresh=cJSON_IsTrue(get(st,"fresh")),error_free=!strcmp(str(st,"error","unknown"),"NoError");
 bool reboot=!strcmp(action,"reboot"),native_idle_verified=false;
 if(reboot&&idle&&error_free&&!blocked&&cJSON_IsTrue(get(st,"cache_available"))&&cJSON_IsTrue(get(st,"local_link_active"))){
  cJSON *context=restore_helper("restore-context",NULL);native_idle_verified=cJSON_IsTrue(get(context,"idle"))&&strlen(str(context,"id",""))>0;cJSON_Delete(context);
 }
 bool state_recent=fresh||(reboot&&native_idle_verified);
 const char *reason=blocked?"skipped_map_transition":!error_free?"skipped_robot_error":(!idle&&(strcmp(action,"dock")||!dockable_state(work)))?"skipped_robot_busy":!state_recent?"skipped_state_stale":NULL;
 if(reason){
  setstr(j,"last_result",reason);setstr(j,"last_working_status",work);setbool(j,"last_report_fresh",fresh);setbool(j,"last_local_link_active",cJSON_IsTrue(get(st,"local_link_active")));setnum(j,"last_observed_at",num(st,"observed_at",0));
  char message[200];snprintf(message,sizeof(message),"Schedule %d (%s): %s; no retry today",id,action,reason);event("schedule",message);cJSON_Delete(st);save_state();error_response(fd,409,reason);return;
 }
 if(reboot){setstr(j,"last_state_check",native_idle_verified?"native_idle_cache_and_active_link":"fresh_idle_report");}
 cJSON_Delete(st);
 if(restoring){
  cJSON *copy=schedule_master(str(j,"master_uid","")),*context=restore_helper("restore-context",NULL);
  if(!copy||strcmp(str(copy,"revision",""),str(j,"master_revision",""))||!cJSON_IsTrue(get(context,"idle"))){cJSON_Delete(copy);cJSON_Delete(context);setstr(j,"last_result","skipped_master_unavailable");event("schedule","Scheduled master restore skipped: copy or native identity unavailable");save_state();error_response(fd,409,"Scheduled master copy is unavailable or changed");return;}
  cJSON *request=cJSON_CreateObject();setstr(request,"uid",str(copy,"uid",""));setstr(request,"version",str(copy,"version",""));setstr(request,"active_id",str(context,"id",""));setbool(request,"confirm",true);setstr(request,"confirmation_text","RESTORE");cJSON_Delete(copy);cJSON_Delete(context);
  const char *keys[]={"operation_uid","active_backup_uid","target_backup_uid"};for(int i=0;i<3;i++){char uid[33];if(!random_uid(uid)){cJSON_Delete(request);setstr(j,"last_result","failed_random_source");save_state();error_response(fd,500,"Restore not sent: random source unavailable");return;}setstr(request,keys[i],uid);}
  bool ok=restore_master(fd,request);cJSON_Delete(request);setstr(j,"last_result",ok?"restore_dispatched_awaiting_confirmation":"restore_failed_or_uncertain");save_state();return;
 }
 char err[200]={0};bool ok;
 if(!strcmp(action,"reboot")){
  event("schedule","Scheduled reboot dispatched through zigmactl (AM380 and Linux)");setstr(j,"last_result","reboot_dispatch_recorded");if(!save_state()){error_response(fd,500,"Cannot record reboot; no command sent");return;}
  char *args[]={(char*)control_bin,"reboot",NULL};ok=!strcmp(mode,"demo")|| (control_bin&&run_process(args,NULL,0)==0);
 }else ok=issue(!strcmp(action,"dock")?"dock":"clean",j,err,sizeof(err));
 setstr(j,"last_result",ok?"dispatched":"failed_or_uncertain");if(!ok)event("error",*err?err:"Scheduled action failed or its result is uncertain; no retry");save_state();
 cJSON *o=cJSON_CreateObject();cJSON_AddBoolToObject(o,"accepted",ok);cJSON_AddStringToObject(o,"action",action);json_response(fd,ok?200:502,o);
}
#include "zigma_settings_extra.h"
#include "zigma_telegram.h"
static cJSON *settings_json(void){
 cJSON *o=NULL;
 if(settings_bin){char buf[2048]={0};char *args[]={(char*)settings_bin,"get",NULL};if(!run_process(args,buf,sizeof(buf)))o=strict_json(buf);}
 if(!cJSON_IsObject(o)){cJSON_Delete(o);o=cJSON_CreateObject();setstr(o,"hostname",str(state,"hostname","demo-robot"));setstr(o,"timezone",str(state,"timezone",current_zone));}
 const char *zone=str(o,"timezone",current_zone);if(strlen(zone)<sizeof(current_zone)){snprintf(current_zone,sizeof(current_zone),"%s",zone);setenv("TZ",current_zone,1);tzset();}
 setstr(o,"robot_name",str(state,"robot_name",robot));setbool(o,"editable",!strcmp(mode,"demo")||settings_bin);setbool(o,"simulated",!strcmp(mode,"demo"));
 setstr(o,"area_unit",str(state,"area_unit","m2"));
 cJSON_AddItemToObject(o,"timezones",timezone_list());setbool(o,"ssh_available",ssh_keys_file!=NULL);setbool(o,"audio_available",voices_dir!=NULL);
 cJSON *telemetry=NULL;
 if(status_file){char *raw=read_file(status_file,LIMIT);if(raw){telemetry=strict_json(raw);free(raw);}}
 for(const char **k=(const char*[]){"volume","over_ridge","continue_clean",NULL};*k;k++){
  cJSON *v=get(!strcmp(mode,"demo")?state:telemetry,*k);if(v)cJSON_AddItemToObject(o,*k,cJSON_Duplicate(v,1));
 }
 setbool(o,"options_editable",!strcmp(mode,"demo")||options_bin!=NULL);cJSON_Delete(telemetry);
 if(!strcmp(mode,"demo")){setstr(o,"ntp_server",str(state,"ntp_server","time.google.com"));setbool(o,"ntp_enabled",true);}
 time_t now=time(NULL);struct tm local;localtime_r(&now,&local);char dt[32];strftime(dt,sizeof(dt),"%Y-%m-%dT%H:%M:%S",&local);setstr(o,"local_datetime",dt);setnum(o,"epoch",now);return o;
}
static bool settings_apply(cJSON *input,char *error,size_t cap){
 const char *action=str(input,"action",""),*value=str(input,"value","");char host[64]={0};const char *arg=value;
 bool local=!strcmp(action,"area_unit"),option=!strcmp(action,"volume")||!strcmp(action,"over_ridge")||!strcmp(action,"continue_clean");
 if(strcmp(action,"name")&&strcmp(action,"timezone")&&strcmp(action,"ntp_server")&&strcmp(action,"ntp_sync")&&!local&&!option){snprintf(error,cap,"Unknown setting; the clock uses automatic NTP");return false;}
 if(!strcmp(mode,"commands")&&!local&&!(option?options_bin:settings_bin)){snprintf(error,cap,"Settings adapter unavailable");return false;}
 if(!strcmp(action,"name")){
  size_t n=strlen(value),j=0;if(n<1||n>48)goto invalid;
  for(size_t i=0;i<n;i++){unsigned char c=value[i];if(c>=128||(!isalnum(c)&&c!=' '&&c!='-'))goto invalid;if(c==' '||c=='-'){if(j&&host[j-1]!='-')host[j++]='-';}else host[j++]=(char)tolower(c);}
  while(j&&host[j-1]=='-')j--;
  host[j]=0;if(!j)goto invalid;arg=host;
 }else if(!strcmp(action,"timezone")){
  if(!*value||strlen(value)>=sizeof(current_zone)||strstr(value,"..")||value[0]=='/')goto invalid;
  for(const char *p=value;*p;p++)if(!isalnum((unsigned char)*p)&&!strchr("/_+-",*p))goto invalid;
  char file[256];snprintf(file,sizeof(file),"/usr/share/zoneinfo/%s",value);FILE *zf=fopen(file,"rb");if(!zf)goto invalid;char magic[4];bool valid=fread(magic,1,4,zf)==4&&!memcmp(magic,"TZif",4);fclose(zf);if(!valid)goto invalid;
 }else if(local){if(strcmp(value,"m2")&&strcmp(value,"ft2"))goto invalid;
 }else if(!strcmp(action,"ntp_server")){
  size_t n=strlen(value);if(!n||n>253||!isalnum((unsigned char)value[0])||!isalnum((unsigned char)value[n-1])||strstr(value,".."))goto invalid;
  for(const char *p=value;*p;p++)if(!isalnum((unsigned char)*p)&&*p!='.'&&*p!='-')goto invalid;
 }else if(!strcmp(action,"volume")){
  char *end;long n=strtol(value,&end,10);if(!*value||*end||n<0||n>100)goto invalid;
 }else if(option){if(strcmp(value,"true")&&strcmp(value,"false"))goto invalid;}
 if(!strcmp(mode,"commands")&&!local){char *args[]={(char*)(option?options_bin:settings_bin),(char*)action,(char*)arg,NULL};if(run_process(args,NULL,0)){snprintf(error,cap,"Setting failed or is already running. Refresh before retrying.");return false;}}
 if(!strcmp(action,"name")){setstr(state,"robot_name",value);setstr(state,"hostname",host);}
 if(!strcmp(action,"timezone")){setstr(state,"timezone",value);snprintf(current_zone,sizeof(current_zone),"%s",value);setenv("TZ",current_zone,1);tzset();}
 if(local||!strcmp(action,"ntp_server"))setstr(state,action,value);
 if(option&&!strcmp(mode,"demo")){if(!strcmp(action,"volume"))setnum(state,action,atoi(value));else setbool(state,action,!strcmp(value,"true"));}
 event("settings",option?"Robot setting dispatched":"Settings updated");if(!save_state()){snprintf(error,cap,"Setting processed but state could not be saved; refresh before retrying");return false;}return true;
invalid:snprintf(error,cap,"Invalid setting value");return false;
}
static void migrate_event_language(void){cJSON *item;cJSON_ArrayForEach(item,get(state,"events")){const char *message=str(item,"message","");
 if(!strcmp(message,"En la base · simulación")){setstr(item,"message","At the dock · simulation");continue;}
 if(!strcmp(message,"Orden entregada al control local; ejecución física pendiente de telemetría")){setstr(item,"message","Command dispatched to local control; waiting for robot feedback");continue;}
 if(!strcmp(message,"Zonas guardadas · simulación")){setstr(item,"message","No-go zones saved · simulation");continue;}
 if(!strcmp(message,"Limpieza reanudada")){setstr(item,"message","Cleaning resumed");continue;}
 if(!strcmp(message,"Limpieza iniciada")){setstr(item,"message","Cleaning started");continue;}
 if(!strcmp(message,"Limpieza de área")){setstr(item,"message","Area cleaning");continue;}
 if(!strcmp(message,"Pausa")){setstr(item,"message","Pause");continue;}
 if(!strcmp(message,"Regreso a la base")){setstr(item,"message","Returning to dock");continue;}
 if(!strcmp(message,"Horario omitido: robot ocupado o estado sin confirmar")){setstr(item,"message","Schedule skipped: robot busy or state unconfirmed");continue;}
 if(!strcmp(message,"Horarios guardados")){setstr(item,"message","Schedules saved");continue;}
 if(!strcmp(message,"Agente local iniciado")){setstr(item,"message","Local agent started");continue;}
 if(!strcmp(message,"Esta función todavía no tiene adaptador real")){setstr(item,"message","This feature has no real adapter yet");continue;}
 if(!strcmp(message,"El control local devolvió un error o agotó el tiempo")){setstr(item,"message","Local control failed or timed out");continue;}
 if(!strcmp(message,"Orden no admitida")){setstr(item,"message","Unsupported command");continue;}
 if(!strcmp(message,"Selecciona un rectángulo válido dentro del mapa")){setstr(item,"message","Select a valid rectangle inside the map");continue;}
 if(!strcmp(message,"Zona no válida")){setstr(item,"message","Invalid zone");continue;}
 if(!strcmp(message,"Máximo 12 zonas")){setstr(item,"message","Maximum 12 zones");continue;}
 if(!strcmp(message,"Máximo 24 horarios")){setstr(item,"message","Maximum 24 schedules");continue;}
 if(!strcmp(message,"Días no válidos")){setstr(item,"message","Invalid weekdays");continue;}
 if(!strcmp(message,"El horario ya no existe; recarga la página")){setstr(item,"message","Schedule no longer exists; reload the page");continue;}
 if(!strcmp(message,"Horario desactivado o fuera de su minuto de ejecución")){setstr(item,"message","Schedule disabled or outside its due minute");continue;}
 if(!strcmp(message,"Horario no encontrado")){setstr(item,"message","Schedule not found");continue;}
 if(!strcmp(message,"Host no admitido")){setstr(item,"message","Host not allowed");continue;}
 if(!strcmp(message,"Identificador no válido")){setstr(item,"message","Invalid identifier");continue;}
 if(!strcmp(message,"Introduce la clave de acceso del agente")){setstr(item,"message","Enter the robot access password");continue;}
 if(!strcmp(message,"JSON no válido o con claves repetidas")){setstr(item,"message","Invalid JSON or duplicate keys");continue;}
 if(!strcmp(message,"Método no admitido")){setstr(item,"message","Method not allowed");continue;}
 if(!strcmp(message,"No se puede registrar la ejecución; no se envía la orden")){setstr(item,"message","Cannot record execution; no command sent");continue;}
 if(!strcmp(message,"Nombre no válido")){setstr(item,"message","Invalid name");continue;}
 if(!strcmp(message,"Orden procesada pero no guardada; no la repitas automáticamente")){setstr(item,"message","Command processed but not saved; do not retry automatically");continue;}
 if(!strcmp(message,"Origen no admitido")){setstr(item,"message","Origin not allowed");continue;}
 if(!strcmp(message,"Petición demasiado grande")){setstr(item,"message","Request too large");continue;}
 if(!strcmp(message,"Petición no válida")){setstr(item,"message","Invalid request");continue;}
 if(!strcmp(message,"Revisa nombre, hora y días del horario")){setstr(item,"message","Check schedule name, time and weekdays");continue;}
 if(!strcmp(message,"Se requiere JSON y X-Zigma-Request")){setstr(item,"message","JSON and X-Zigma-Request are required");continue;}
 if(!strcmp(message,"Crontab pendiente de instalación")){setstr(item,"message","Crontab installation pending");continue;}
}}
static bool access_key_valid(const char *key){size_t n=strlen(key);if(n<12||n>128)return false;for(const unsigned char *p=(const unsigned char*)key;*p;p++)if(*p<33||*p>126)return false;return true;}
static cJSON *native_map_id(void);
static cJSON *restore_helper(const char *op,cJSON *input);
static bool update_map_reset(void){
 if(!reset_waiting())return false;
 cJSON *pending=get(state,"map_reset_pending"),*map=demo_map(),*native=native_map_id();const char *id=str(map,"map_id","");
 bool restoring=!strcmp(str(pending,"operation",""),"restore");
 bool recovered=cJSON_IsTrue(get(map,"available"))&&cJSON_IsArray(get(map,"cells"))&&strlen(id)&&!strcmp(id,str(native,"id",""))&&cJSON_IsNumber(get(map,"snapshot_read_at"))&&get(map,"snapshot_read_at")->valuedouble>num(pending,"at",0);
 if(restoring){
  cJSON *probe=restore_helper("restore-probe",pending);
  bool seen=cJSON_IsTrue(get(probe,"result_seen")),success=cJSON_IsTrue(get(probe,"restored"));
  if(seen&&!success&&strcmp(str(pending,"phase",""),"restore_failed")){setstr(pending,"phase","restore_failed");save_state();}
  if(seen&&success&&!strcmp(str(probe,"id",""),str(pending,"id",""))&&!cJSON_IsTrue(get(pending,"native_confirmed"))){setbool(pending,"native_confirmed",true);save_state();}
  recovered=recovered&&cJSON_IsTrue(get(pending,"native_confirmed"))&&!strcmp(id,str(pending,"id",""))&&!strcmp(id,str(probe,"id",""))&&cJSON_IsNumber(get(probe,"result_at"))&&num(map,"snapshot_read_at",0)>=num(probe,"result_at",2147483647)&&strcmp(str(pending,"phase",""),"restore_failed");
  cJSON_Delete(probe);
 }else recovered=recovered&&strcmp(id,str(pending,"id",""));
 cJSON_Delete(native);cJSON_Delete(map);
 if(recovered){setstr(pending,"phase","new_map_observed");setnum(pending,"completed_at",time(NULL));event("maps",restoring?"Master restored: native success and new map publication observed":"New map observed after clear request");save_state();}
 return recovered;
}
static cJSON *reset_placeholder(void){
 cJSON *o=cJSON_CreateObject();setbool(o,"available",false);setbool(o,"reset_pending",true);setstr(o,"source","map_reset_guard");setstr(o,"warning",!strcmp(str(get(state,"map_reset_pending"),"operation",""),"restore")?"Master restoration pending. Waiting for native confirmation and a new map. No cleaning started. Use Refresh maps to check progress.":"Clear request recorded. Old map hidden while waiting for a new robot map. No cleaning was started automatically.");return o;
}
static void map_response(int fd,const char *request){
 /* Cache the validated wire document, invalidating on atomic file replacement
    or when a recent position estimate expires. Authentication happens first. */
 static struct stat previous;static char *wire=NULL;static double expires=0;static unsigned revision=0;
 if(reset_waiting()){if(update_map_reset()){free(wire);wire=NULL;expires=0;}else{json_response(fd,200,reset_placeholder());return;}}
 struct stat current;struct timespec ts;clock_gettime(CLOCK_REALTIME,&ts);double now=ts.tv_sec+ts.tv_nsec/1e9;
 if(!map_file||stat(map_file,&current)){json_response(fd,200,demo_map());return;}
 bool changed=!wire||previous.st_ino!=current.st_ino||previous.st_size!=current.st_size||previous.st_mtim.tv_sec!=current.st_mtim.tv_sec||previous.st_mtim.tv_nsec!=current.st_mtim.tv_nsec||(expires&&(now>=expires||now<expires-6));
 if(changed){cJSON *map=demo_map();cJSON *at=get(get(map,"position_estimate"),"received_at");expires=cJSON_IsNumber(at)?at->valuedouble+6:0;
  char *next=cJSON_PrintUnformatted(map);cJSON_Delete(map);if(!next){error_response(fd,500,"Map encoding failed");return;}
  free(wire);wire=next;previous=current;revision++;
 }
 char tag[96],match[128],head[512];snprintf(tag,sizeof(tag),"\"map-%ld-%u\"",(long)getpid(),revision);
 bool unchanged=header_value(request,"If-None-Match",match,sizeof(match))&&!strcmp(match,tag);
 snprintf(head,sizeof(head),"HTTP/1.1 %d %s\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nETag: %s\r\nCache-Control: private, no-cache\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n",unchanged?304:200,unchanged?"Not Modified":"OK",strlen(wire),tag);
 send_all(fd,head,strlen(head));if(!unchanged)send_all(fd,wire,strlen(wire));
}
/* Catalog and saved-file previews are demand-driven, never part of telemetry polling. */
static bool saved_id(const char *id){
 if(!id||!*id||strlen(id)>10||(*id=='0'&&id[1]))return false;
 unsigned long long n=0;for(const char *p=id;*p;p++){if(*p<'0'||*p>'9')return false;n=n*10+(unsigned)(*p-'0');if(n>2147483647)return false;}return true;
}
static cJSON *saved_maps(const char *id){
 if(!maps_bin||!maps_dir)return NULL;
 size_t cap=id?400000:32768;char *buf=calloc(cap,1);if(!buf)return NULL;
 char *argv[]={(char*)maps_bin,id?"preview":"list",(char*)maps_dir,(char*)id,NULL};
 cJSON *o=NULL;if(!run_process(argv,buf,cap))o=strict_json(buf);free(buf);
 if(!cJSON_IsObject(o)||(!id&&!cJSON_IsArray(get(o,"items")))){cJSON_Delete(o);return NULL;}return o;
}
static cJSON *maps_helper_json(char *const args[],size_t cap){
 char *buf=calloc(cap,1);if(!buf)return NULL;cJSON *o=NULL;if(!run_process(args,buf,cap))o=strict_json(buf);free(buf);if(!cJSON_IsObject(o)){cJSON_Delete(o);return NULL;}return o;
}
static cJSON *map_library(const char *operation,cJSON *input){
 if(!maps_bin||!map_library_dir)return NULL;
 char *json=cJSON_PrintUnformatted(input);if(!json)return NULL;
 char *args[]={(char*)maps_bin,"library",(char*)map_library_dir,(char*)operation,json,NULL};cJSON *out=maps_helper_json(args,!strcmp(operation,"preview")?400000:32768);free(json);return out;
}
static cJSON *native_map_id(void){
 if(!maps_bin||!maps_app_pid)return NULL;
 char *args[]={(char*)maps_bin,"active",(char*)maps_app_pid,NULL};return maps_helper_json(args,2048);
}
static cJSON *restore_helper(const char *op,cJSON *input){
 if(!maps_bin||!maps_app_pid)return NULL;
 char *json=input?cJSON_PrintUnformatted(input):NULL;char *args[]={(char*)maps_bin,(char*)op,(char*)maps_app_pid,json,NULL};cJSON *out=maps_helper_json(args,8192);free(json);return out;
}
static void library_response(int fd,cJSON *result){
 if(!result){error_response(fd,503,"Master library unavailable; refresh before retrying");return;}
 if(get(result,"error")){char message[240];snprintf(message,sizeof(message),"%s",str(result,"error","Library operation failed"));cJSON_Delete(result);error_response(fd,409,message);return;}
 json_response(fd,200,result);
}
static bool restore_master(int fd,cJSON *input){
 if(strcmp(mode,"commands")||!map_library_dir||!maps_dir||!maps_app_pid||!cJSON_IsTrue(get(input,"confirm"))||strcmp(str(input,"confirmation_text",""),"RESTORE")){error_response(fd,400,"Restore requires explicit RESTORE confirmation and a configured robot");return false;}
 cJSON *old=get(state,"map_reset_pending");
 if(reset_waiting()&&!strcmp(str(old,"operation",""),"restore")&&strcmp(str(old,"phase",""),"restore_failed")){error_response(fd,409,"A restoration is already awaiting confirmation. No order repeated");return false;}
 cJSON *st=status_json();const char *work=str(st,"working_status","");bool ready=(cJSON_IsTrue(get(st,"fresh"))||(cJSON_IsTrue(get(st,"cache_available"))&&cJSON_IsTrue(get(st,"local_link_active"))))&&!strcmp(str(st,"error",""),"NoError")&&(!strcmp(work,"PileCharging")||!strcmp(work,"DirCharging")||!strcmp(work,"Standby")||!strcmp(work,"Hibernating")||!strcmp(work,"charging")||!strcmp(work,"idle"));cJSON_Delete(st);
 cJSON *context=restore_helper("restore-context",NULL);
 if(!ready||!cJSON_IsTrue(get(context,"idle"))||!saved_id(str(context,"id",""))||strcmp(str(input,"active_id",""),str(context,"id",""))||!cJSON_IsNumber(get(context,"log_inode"))||!cJSON_IsNumber(get(context,"log_offset"))){cJSON_Delete(context);error_response(fd,409,"Restore requires an idle robot, recent local communication and unchanged map identity. Refresh maps");return false;}
 cJSON *copy=map_library("preview",input);if(!copy||get(copy,"error")){cJSON_Delete(context);library_response(fd,copy);return false;}
 cJSON *pending=cJSON_Duplicate(context,1);cJSON_Delete(context);setstr(pending,"operation_uid",str(input,"operation_uid",""));setstr(pending,"operation","restore");setstr(pending,"phase","prepared");setstr(pending,"id",str(copy,"id",""));setstr(pending,"uid",str(input,"uid",""));setnum(pending,"width",num(copy,"width",0));setnum(pending,"height",num(copy,"height",0));setnum(pending,"at",time(NULL));cJSON_Delete(copy);
 cJSON *previous=old?cJSON_Duplicate(old,1):NULL;cJSON_DeleteItemFromObjectCaseSensitive(state,"map_reset_pending");cJSON_AddItemToObject(state,"map_reset_pending",pending);
 if(!save_state()){cJSON_DeleteItemFromObjectCaseSensitive(state,"map_reset_pending");if(previous)cJSON_AddItemToObject(state,"map_reset_pending",previous);error_response(fd,500,"Restore could not be recorded; no order sent");return false;}
 cJSON *request=cJSON_Duplicate(input,1);setstr(request,"library_dir",map_library_dir);setstr(request,"source_dir",maps_dir);cJSON *result=restore_helper("restore-native",request);cJSON_Delete(request);
 bool sent=result&&cJSON_IsTrue(get(result,"accepted"));
 if(result&&!sent){cJSON_DeleteItemFromObjectCaseSensitive(state,"map_reset_pending");if(previous)cJSON_AddItemToObject(state,"map_reset_pending",previous);previous=NULL;}
 else{setstr(pending,"phase",sent?"dispatched":"uncertain");event("maps",sent?"Master restore dispatched; waiting for native confirmation. No cleaning started":"Restore delivery uncertain. Do not retry automatically");}
 cJSON_Delete(previous);save_state();if(result)setbool(result,"restore_pending",sent);library_response(fd,result);return sent;
}
static void delete_saved_map(int fd,cJSON *input,bool clear){
 if(strcmp(mode,"commands")){error_response(fd,400,"Native map changes require commands mode");return;}
 bool backup_required=!cJSON_IsFalse(get(input,"backup"));
 if(get(input,"backup")&&!cJSON_IsBool(get(input,"backup"))){error_response(fd,400,"backup must be a boolean");return;}
 const char *id=str(input,"id",""),*revision=str(input,"revision","");
 if(!saved_id(id)||!cJSON_IsTrue(get(input,"confirm"))||(backup_required&&!map_library_dir)||!maps_app_pid||(clear&&strcmp(str(input,"confirmation_text",""),"CLEAR"))){error_response(fd,400,"Confirmed deletion requires a master library and native map identity");return;}
 if(reset_waiting()){error_response(fd,409,"A clear request is already awaiting a new map; no order was repeated");return;}
 cJSON *st=status_json();const char *work=str(st,"working_status","");bool ready=(cJSON_IsTrue(get(st,"fresh"))||(cJSON_IsTrue(get(st,"cache_available"))&&cJSON_IsTrue(get(st,"local_link_active"))))&&!strcmp(str(st,"error",""),"NoError")&&(!strcmp(work,"PileCharging")||!strcmp(work,"DirCharging")||!strcmp(work,"Standby")||!strcmp(work,"Hibernating")||!strcmp(work,"charging")||!strcmp(work,"idle"));cJSON_Delete(st);
 if(!ready){error_response(fd,409,"Deleting a robot map requires an idle robot with recent local communication and no error");return;}
 cJSON *active=native_map_id();const char *current=str(active,"id","");
 if(!saved_id(current)||(clear?strcmp(id,current):!strcmp(id,current))||strcmp(str(input,"active_id",""),current)){cJSON_Delete(active);error_response(fd,409,"Map identity changed or this action is not valid for that map. Refresh maps");return;}
 cJSON_Delete(active);cJSON *pending=get(state,"map_delete_pending");double since=difftime(time(NULL),(time_t)num(pending,"at",0));
 if(pending&&since<30){error_response(fd,409,"Previous map deletion is still awaiting confirmation. Refresh maps before retrying");return;}
 cJSON *catalog=saved_maps(NULL),*candidate;bool matching=false;
 cJSON_ArrayForEach(candidate,get(catalog,"items"))if(!strcmp(str(candidate,"id",""),id)&&!strcmp(str(candidate,"revision",""),revision))matching=true;
 cJSON_Delete(catalog);if(!matching){error_response(fd,409,"Map changed; no deletion sent");return;}
 cJSON *request=cJSON_Duplicate(input,1);setstr(request,"source_dir",maps_dir);setstr(request,"kind",clear?"reset_backup":"deletion_backup");
 if(backup_required){
 cJSON *backup=map_library("save",request);
 if(!backup||get(backup,"error")){cJSON_Delete(request);library_response(fd,backup);return;}
 cJSON_Delete(backup);
 }
 if(clear){cJSON *reset=cJSON_CreateObject();setstr(reset,"id",id);setstr(reset,"revision",revision);setstr(reset,"phase","prepared");setstr(reset,"backup_uid",backup_required?str(input,"uid",""):"");setnum(reset,"at",time(NULL));cJSON_DeleteItemFromObjectCaseSensitive(state,"map_reset_pending");cJSON_AddItemToObject(state,"map_reset_pending",reset);}
 cJSON *entry=cJSON_CreateObject();setstr(entry,"id",id);setstr(entry,"revision",revision);setstr(entry,"backup_uid",backup_required?str(input,"uid",""):"");setnum(entry,"at",time(NULL));cJSON_DeleteItemFromObjectCaseSensitive(state,"map_delete_pending");cJSON_AddItemToObject(state,"map_delete_pending",entry);
 if(!save_state()){if(clear){setstr(get(state,"map_reset_pending"),"phase","not_sent");save_state();}cJSON_Delete(request);error_response(fd,500,"Deletion could not be recorded. No order sent");return;}
 setstr(request,"library_dir",map_library_dir?map_library_dir:"");char *json=cJSON_PrintUnformatted(request);cJSON_Delete(request);
 if(!json){if(clear){setstr(get(state,"map_reset_pending"),"phase","not_sent");save_state();}error_response(fd,500,"Deletion was not encoded");return;}
 char *args[]={(char*)maps_bin,clear?"clear-native":"delete-native",(char*)maps_app_pid,json,NULL};cJSON *result=maps_helper_json(args,4096);free(json);
 if(clear){setstr(get(state,"map_reset_pending"),"phase",result?(cJSON_IsTrue(get(result,"accepted"))?"dispatched":"not_sent"):"uncertain");save_state();}
 if(result&&cJSON_IsTrue(get(result,"accepted"))){setstr(result,"backup_uid",backup_required?str(input,"uid",""):"");setbool(result,"backup_saved",backup_required);setbool(result,"map_reset_pending",clear);event("maps",clear?"Clear current map dispatched. Waiting for a new map; no cleaning started":"Map deletion dispatched. Refresh to confirm");save_state();}
 library_response(fd,result);
}
static const char *saved_name(const char *id){return str(get(state,"map_names"),id,"");}
static cJSON *maps_catalog(void){
 update_map_reset();
 cJSON *o=saved_maps(NULL);if(!o)return NULL;
 cJSON *item;cJSON_ArrayForEach(item,get(o,"items"))setstr(item,"name",saved_name(str(item,"id","")));
 cJSON *map=demo_map();const char *id=str(map,"map_id","");
 if(saved_id(id)&&!reset_waiting())cJSON_AddStringToObject(o,"last_observed_active_id",id);else cJSON_AddNullToObject(o,"last_observed_active_id");
 cJSON_Delete(map);cJSON *native=native_map_id();const char *active=str(native,"id","");
 if(saved_id(active)&&!reset_waiting())cJSON_AddStringToObject(o,"native_active_id",active);else cJSON_AddNullToObject(o,"native_active_id");
 cJSON_AddBoolToObject(o,"delete_available",!reset_waiting()&&saved_id(active)&&map_library_dir);cJSON_AddBoolToObject(o,"clear_available",!reset_waiting()&&saved_id(active)&&map_library_dir);cJSON_AddBoolToObject(o,"reset_pending",reset_waiting());cJSON_AddBoolToObject(o,"library_available",map_library_dir!=NULL);cJSON_Delete(native);
 cJSON_AddStringToObject(o,"active_source","observed_map_cache");cJSON_AddNumberToObject(o,"read_at",time(NULL));
 cJSON *context=restore_helper("restore-context",NULL);if(context)cJSON_AddItemToObject(o,"restore_context",context);
 cJSON_AddBoolToObject(o,"restore_available",!strcmp(mode,"commands")&&map_library_dir&&context&&cJSON_IsTrue(get(context,"idle"))&&(!reset_waiting()||strcmp(str(get(state,"map_reset_pending"),"operation",""),"restore")||!strcmp(str(get(state,"map_reset_pending"),"phase",""),"restore_failed")));
 cJSON *transition=get(state,"map_reset_pending");if(transition&&!strcmp(str(transition,"operation",""),"restore"))cJSON_AddItemToObject(o,"restoration",cJSON_Duplicate(transition,1));
 cJSON_AddStringToObject(o,"selection_reason","Restore a protected master copy with explicit confirmation.");
 cJSON_AddStringToObject(o,"name_scope","zigma_free");return o;
}
static void rename_saved_map(int fd,cJSON *input){
 const char *id=str(input,"id",""),*name=str(input,"name",""),*revision=str(input,"revision","");size_t len=strlen(name);bool visible=false;
 if(!saved_id(id)||!len||len>96){error_response(fd,400,"Choose a map and a name of 1 to 96 UTF-8 bytes");return;}
 for(const unsigned char *p=(const unsigned char*)name;*p;p++){if(*p<32||*p==127){error_response(fd,400,"Invalid map name");return;}if(*p!=' ')visible=true;}
 if(!visible){error_response(fd,400,"Map name cannot be blank");return;}
 cJSON *catalog=saved_maps(NULL),*item=NULL,*found=NULL;
 if(!catalog){error_response(fd,503,"Saved maps could not be read; no name was changed");return;}
 cJSON_ArrayForEach(item,get(catalog,"items"))if(!strcmp(str(item,"id",""),id))found=item;
 if(!found||strcmp(str(found,"revision",""),revision)||!cJSON_IsString(get(input,"previous_name"))||strcmp(saved_name(id),str(input,"previous_name",""))){cJSON_Delete(catalog);error_response(fd,409,"Map or name changed. Refresh the catalog before saving");return;}
 cJSON_Delete(catalog);cJSON *before=cJSON_Duplicate(state,1),*names=get(state,"map_names");if(!before){error_response(fd,500,"Unable to save map name");return;}
 if(!cJSON_IsObject(names)){cJSON_DeleteItemFromObjectCaseSensitive(state,"map_names");names=cJSON_AddObjectToObject(state,"map_names");}
 setstr(names,id,name);event("maps","Saved map renamed in Zigma Free");
 if(!save_state()){cJSON_Delete(state);state=before;error_response(fd,500,"Map name was not saved");return;}
 cJSON_Delete(before);cJSON *result=cJSON_CreateObject();cJSON_AddStringToObject(result,"id",id);cJSON_AddStringToObject(result,"name",name);cJSON_AddStringToObject(result,"name_scope","zigma_free");json_response(fd,200,result);
}
 
#include "zigma_sessions_api.h"
#include "zigma_release.h"
#include "zigma_laser_api.h"
/* A streaming backup runs in a child; status and controls remain responsive. */
static pid_t nor_pid=0;
static void nor_download(int fd){
 if(nor_pid>0){int status;pid_t done=waitpid(nor_pid,&status,WNOHANG);if(done==0){error_response(fd,409,"A NOR backup is already running");return;}nor_pid=0;}
 pid_t child=fork();if(child<0){error_response(fd,503,"Cannot start NOR backup");return;}
 if(child==0){
  pid_t parent=getppid();prctl(PR_SET_PDEATHSIG,SIGTERM);if(getppid()!=parent)_exit(1);
  struct timeval timeout={30,0};setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);
  const char *head="HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n";
  send_all(fd,head,strlen(head));if(dup2(fd,STDOUT_FILENO)<0)_exit(1);
  int null=open("/dev/null",O_RDWR);if(null>=0){dup2(null,STDIN_FILENO);dup2(null,STDERR_FILENO);}
  long limit=sysconf(_SC_OPEN_MAX);if(limit<0)limit=1024;for(int n=3;n<limit;n++)close(n);
  execl(executable,executable,"--nor-stream",(char*)NULL);_exit(127);
 }
 nor_pid=child;
}
static void api(int fd,const char *method,const char *url,const char *body,const char *request){
 if(!strcmp(method,"GET")&&!strcmp(url,"/api/backup/nor")){nor_download(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(url,"/api/about")){json_response(fd,200,release_view());return;}
 if(!strcmp(method,"GET")&&!strcmp(url,"/api/laser")){json_response(fd,200,laser_response());return;}
 if(sessions_api(fd,method,url))return;
 if(!strcmp(method,"GET")&&!strcmp(url,"/api/telegram")){json_response(fd,200,tg_view());return;}
 if(!strcmp(method,"GET")&&!strcmp(url,"/api/map-library")){cJSON *r=cJSON_CreateObject();cJSON *out=map_library("list",r);cJSON_Delete(r);library_response(fd,out);return;}
 if(!strcmp(method,"GET")&&!strncmp(url,"/api/map-library/",17)){
  const char *suffix=url+17;size_t len=strlen(suffix);char uid[33]={0};bool valid=len==40&&!strcmp(suffix+32,"/preview");
  if(valid){memcpy(uid,suffix,32);for(int i=0;i<32;i++)if(!strchr("0123456789abcdef",uid[i]))valid=false;}
  if(!valid){error_response(fd,404,"Master copy not found");return;}cJSON *r=cJSON_CreateObject();setstr(r,"uid",uid);cJSON *out=map_library("preview",r);cJSON_Delete(r);library_response(fd,out);return;
 }

 if(!strcmp(method,"GET")&&!strcmp(url,"/api/maps")){cJSON *o=maps_catalog();if(o)json_response(fd,200,o);else error_response(fd,503,"Saved-map catalog unavailable");return;}
 if(!strcmp(method,"GET")&&!strncmp(url,"/api/maps/",10)){
  char id[16]={0},suffix[32]={0};if(sscanf(url+10,"%15[0-9]%31s",id,suffix)!=2||!saved_id(id)||strcmp(suffix,"/preview")){error_response(fd,404,"Saved map not found");return;}
  cJSON *o=saved_maps(id);if(o)json_response(fd,200,o);else error_response(fd,503,"Saved map is missing, changed or could not be decoded. Refresh the catalog");return;
 }

 if(!strcmp(method,"GET")&&!strcmp(url,"/api/voices")){json_response(fd,200,voices_list());return;}
 if(!strcmp(method,"GET")&&!strncmp(url,"/api/voices/",12)){voice_get(fd,url+12);return;}
 if(!strcmp(method,"GET")&&!strcmp(url,"/api/settings")){json_response(fd,200,settings_json());return;}
 if(!strcmp(method,"GET")&&!strcmp(url,"/api/status")){json_response(fd,200,status_json());return;}
 if(!strcmp(method,"GET")&&!strcmp(url,"/api/map")){char marker[1024];if(map_file){snprintf(marker,sizeof(marker),"%s",map_file);char *slash=strrchr(marker,'/');if(slash)snprintf(slash+1,sizeof(marker)-(size_t)(slash+1-marker),"map-demand");}else path(marker,sizeof(marker),"map-demand");struct stat demand;if(stat(marker,&demand)||time(NULL)-demand.st_mtime>=3)atomic_write(marker,"active");map_response(fd,request);return;}
 if(!strcmp(method,"GET")&&!strcmp(url,"/api/events")){cJSON *o=cJSON_CreateObject();cJSON_AddItemToObject(o,"items",cJSON_Duplicate(get(state,"events"),1));json_response(fd,200,o);return;}
 if(!strcmp(method,"GET")&&!strcmp(url,"/api/schedules")){json_response(fd,200,schedule_json());return;}
 if(!strcmp(method,"POST")&&!strncmp(url,"/api/schedules/",15)){
  int id=0;char suffix[16]={0};if(sscanf(url+15,"%d%15s",&id,suffix)==2&&!strcmp(suffix,"/run")){scheduled_run(fd,id);return;}
 }
 cJSON *input=strict_json(body);if(!input){error_response(fd,400,"Invalid JSON or duplicate keys");return;}
 if(!strncmp(url,"/api/telegram",13)){tg_api(fd,method,url,input);cJSON_Delete(input);return;}
 if(!strcmp(method,"PUT")&&!strcmp(url,"/api/voices")){voice_put(fd,input,false);cJSON_Delete(input);return;}
 if(!strcmp(method,"POST")&&!strcmp(url,"/api/voices/restore")){voice_put(fd,input,true);cJSON_Delete(input);return;}
 if(!strcmp(method,"POST")&&!strcmp(url,"/api/ssh-keys")){ssh_add(fd,input);cJSON_Delete(input);return;}
 if(!strcmp(method,"POST")&&!strcmp(url,"/api/map-library/activate")){restore_master(fd,input);cJSON_Delete(input);return;}
 if(!strcmp(method,"POST")&&!strcmp(url,"/api/maps/clear")){delete_saved_map(fd,input,true);cJSON_Delete(input);return;}
 if(!strcmp(method,"POST")&&!strcmp(url,"/api/maps/delete")){delete_saved_map(fd,input,false);cJSON_Delete(input);return;}
 if(!strcmp(method,"POST")&&!strncmp(url,"/api/map-library/",17)){
  const char *op=url+17;
  if(strcmp(op,"save")&&strcmp(op,"rename")&&strcmp(op,"trash")&&strcmp(op,"restore")&&strcmp(op,"purge")){cJSON_Delete(input);error_response(fd,404,"Unsupported library operation");return;}
  setstr(input,"source_dir",maps_dir?maps_dir:"");setstr(input,"kind","master");cJSON *out=map_library(op,input);cJSON_Delete(input);library_response(fd,out);return;
 }
 if(!strcmp(method,"PUT")&&!strcmp(url,"/api/maps/name")){rename_saved_map(fd,input);cJSON_Delete(input);return;}
 if(!strcmp(method,"PUT")&&!strcmp(url,"/api/access-key")){
  const char *current=str(input,"current_key",""),*next=str(input,"new_key","");
  if(strcmp(current,token)||!access_key_valid(next)){cJSON_Delete(input);error_response(fd,400,"Check the current password; new passwords need 12–128 printable characters without spaces");return;}
  char file[1024];path(file,sizeof(file),"access.token");if(!atomic_write(file,next)){cJSON_Delete(input);error_response(fd,500,"Password could not be saved; it was not changed");return;}
  snprintf(token,sizeof(token),"%s",next);cJSON_Delete(input);event("settings","Access password changed");save_state();cJSON *result=cJSON_CreateObject();cJSON_AddBoolToObject(result,"changed",true);json_response(fd,200,result);return;
 }
 if(!strcmp(method,"PUT")&&!strcmp(url,"/api/settings")){char err[240];bool ok=settings_apply(input,err,sizeof(err));cJSON_Delete(input);if(!ok){error_response(fd,400,err);return;}json_response(fd,200,settings_json());return;}
 if(!strcmp(method,"POST")&&!strcmp(url,"/api/commands")){
  char err[200];bool ok=issue(str(input,"action",""),input,err,sizeof(err));cJSON_Delete(input);
  if(!ok){error_response(fd,400,err);return;}if(!save_state()){error_response(fd,500,"Command processed but not saved; do not retry automatically");return;}
  cJSON *o=cJSON_CreateObject();cJSON_AddBoolToObject(o,"accepted",true);cJSON_AddNumberToObject(o,"dispatched_at",time(NULL));cJSON_AddStringToObject(o,"result",!strcmp(mode,"demo")?"simulated":"delivered_to_local_queue");cJSON_AddStringToObject(o,"mode",mode);json_response(fd,200,o);return;
 }
 if(!strcmp(method,"PUT")&&!strcmp(url,"/api/schedules")){
  char err[200]={0};cJSON *old=cJSON_Duplicate(state,1),*jobs=validate_jobs(get(input,"items"),err,sizeof(err));cJSON_Delete(input);
  if(!jobs){cJSON_Delete(old);error_response(fd,400,err);return;}
  // Carry duplicate-day guards across label/time edits; one execution per local date.
  cJSON *j,*before;cJSON_ArrayForEach(j,jobs)cJSON_ArrayForEach(before,get(state,"schedules"))if(num(j,"id",0)==num(before,"id",0)&&get(before,"last_started_date")){setstr(j,"last_started_date",str(before,"last_started_date",""));setstr(j,"last_result",str(before,"last_result",""));setnum(j,"last_run_at",num(before,"last_run_at",0));}
  cJSON_ReplaceItemInObjectCaseSensitive(state,"schedules",jobs);setbool(state,"cron_applied",false);event("schedule","Schedules saved");
  if(!save_state()){cJSON_Delete(state);state=old;error_response(fd,500,"No se pudieron guardar los horarios");return;}cJSON_Delete(old);
  bool ok=sync_cron(err,sizeof(err));setbool(state,"cron_applied",ok&&cron_dir);bool persisted=save_state();
  if(!ok||!persisted){setbool(state,"cron_applied",false);error_response(fd,503,ok?"No se pudo confirmar el estado del crontab":err);return;}
  json_response(fd,200,schedule_json());return;
 }
 cJSON_Delete(input);error_response(fd,404,"Ruta no disponible");
}
static void handle(int fd){
 static char req[720897];const size_t request_limit=sizeof(req)-1;size_t used=0;char *boundary=NULL;long length=0;bool parsed=false;
 while(used<request_limit){ssize_t n=recv(fd,req+used,request_limit-used,0);if(n<=0)return;used+=(size_t)n;req[used]=0;
  boundary=strstr(req,"\r\n\r\n");if(boundary&&!parsed){char len[32],encoding[64];if(header_value(req,"Transfer-Encoding",encoding,sizeof(encoding))){error_response(fd,400,"Transfer-Encoding no admitido");return;}
   if(header_value(req,"Content-Length",len,sizeof(len))){char *end;length=strtol(len,&end,10);if(*end||length<0||length>716800||(length>16384&&strncmp(req,"PUT /api/voices HTTP/1.1\r\n",strlen("PUT /api/voices HTTP/1.1\r\n")))){error_response(fd,413,"Request too large");return;}}
   parsed=true;}
  if(boundary&&used>=(size_t)(boundary-req+4)+(size_t)length)break;
 }
 if(!boundary){error_response(fd,413,"Cabeceras demasiado grandes");return;}
 char method[12],url[256],version[16];if(sscanf(req,"%11s %255s %15s",method,url,version)!=3){error_response(fd,400,"Invalid request");return;}
 char host[128],origin[256];if(!header_value(req,"Host",host,sizeof(host))||!allowed_host(host)){error_response(fd,403,"Host not allowed");return;}
 if(header_value(req,"Origin",origin,sizeof(origin))){char expected[180];snprintf(expected,sizeof(expected),"http://%s",host);if(strcmp(origin,expected)){error_response(fd,403,"Origin not allowed");return;}}
 bool is_api=!strncmp(url,"/api/",5);
 if(is_api&&!authorized(req)){error_response(fd,401,"Enter the robot access password");return;}
 if(strcmp(method,"GET")){
  char type[128],marker[16];if(!header_value(req,"Content-Type",type,sizeof(type))||strncmp(type,"application/json",16)||!header_value(req,"X-Zigma-Request",marker,sizeof(marker))||strcmp(marker,"1")){error_response(fd,415,"JSON and X-Zigma-Request are required");return;}
 }
 if(is_api){size_t offset=(size_t)(boundary-req)+4;if(offset+(size_t)length>used){error_response(fd,400,"Cuerpo incompleto");return;}boundary[4+length]=0;api(fd,method,url,boundary+4,req);return;}
 if(strcmp(method,"GET")){error_response(fd,405,"Method not allowed");return;}
 const char *file=NULL,*type=NULL;
 if(!strcmp(url,"/")||!strcmp(url,"/index.html")){file="index.html";type="text/html; charset=utf-8";}
 else if(!strcmp(url,"/nor.js")){file="nor.js";type="text/javascript; charset=utf-8";}
 else if(!strcmp(url,"/app.js")){file="app.js";type="text/javascript; charset=utf-8";}
 else if(!strcmp(url,"/style.css")){file="style.css";type="text/css; charset=utf-8";}
 else if(!strcmp(url,"/favicon.svg")){file="favicon.svg";type="image/svg+xml";}
 if(!file){error_response(fd,404,"No encontrado");return;}char p[1024];snprintf(p,sizeof(p),"%s/%s",web_dir,file);char *s=read_file(p,1024*1024);if(!s){error_response(fd,404,"Recurso no disponible");return;}response(fd,200,type,s,strlen(s));free(s);
}
static int cron_client(int id){
 char p[1024];path(p,sizeof(p),"access.token");char *t=read_file(p,128);if(!t)return 1;t[strcspn(t,"\r\n")]=0;
 int fd=socket(AF_INET,SOCK_STREAM,0);struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons((unsigned short)port)};inet_pton(AF_INET,bind_ip,&a.sin_addr);struct timeval timeout={5,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
 if(connect(fd,(struct sockaddr*)&a,sizeof(a))){free(t);close(fd);return 1;}
 char req[1024];snprintf(req,sizeof(req),"POST /api/schedules/%d/run HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nAuthorization: Bearer %s\r\nContent-Type: application/json\r\nX-Zigma-Request: 1\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}",id,port,t);free(t);send_all(fd,req,strlen(req));char res[128]={0};ssize_t n=recv(fd,res,sizeof(res)-1,0);close(fd);if(n>0&&strncmp(res,"HTTP/1.1 200",12))fprintf(stderr,"Schedule dispatch: %.*s\n",(int)strcspn(res,"\r\n"),res);return n>0&&!strncmp(res,"HTTP/1.1 200",12)?0:1;
}
static void stop(int sig){(void)sig;running=0;}
int zigma_nor_main(int argc,char **argv);
int main(int argc,char **argv){
 if(argc==2&&!strcmp(argv[1],"--nor-stream")){char *args[]={argv[0],"--stream",NULL};return zigma_nor_main(2,args);}
 if(zigma_version_only(argc,argv,"free-zigma-agent-arm"))return 0;
 int run_id=0;const char *data_arg=NULL,*web_arg=NULL;
 for(int i=1;i<argc;i++){
  if(i+1>=argc){fprintf(stderr,"Options require a value\n");return 2;}const char *v=argv[++i],*k=argv[i-1];
  if(!strcmp(k,"--release-file"))release_file=v;else if(!strcmp(k,"--core-records-dir"))core_records_dir=v;else if(!strcmp(k,"--telegram-curl"))telegram_curl=v;else if(!strcmp(k,"--telegram-ca"))telegram_ca=v;else if(!strcmp(k,"--voices-dir"))voices_dir=v;else if(!strcmp(k,"--ssh-keys-file"))ssh_keys_file=v;else if(!strcmp(k,"--options-bin"))options_bin=v;else if(!strcmp(k,"--cron-helper"))cron_helper=v;else if(!strcmp(k,"--map-library-dir"))map_library_dir=v;else if(!strcmp(k,"--maps-app-pid"))maps_app_pid=v;else if(!strcmp(k,"--maps-bin"))maps_bin=v;else if(!strcmp(k,"--maps-dir"))maps_dir=v;else if(!strcmp(k,"--zones-bin"))zones_bin=v;else if(!strcmp(k,"--data-dir"))data_arg=v;else if(!strcmp(k,"--web-dir"))web_arg=v;else if(!strcmp(k,"--port"))port=atoi(v);else if(!strcmp(k,"--listen"))bind_ip=v;else if(!strcmp(k,"--name"))robot=v;else if(!strcmp(k,"--mode"))mode=v;else if(!strcmp(k,"--area-bin"))area_bin=v;else if(!strcmp(k,"--settings-bin"))settings_bin=v;else if(!strcmp(k,"--control-bin"))control_bin=v;else if(!strcmp(k,"--status-file"))status_file=v;else if(!strcmp(k,"--map-file"))map_file=v;else if(!strcmp(k,"--cron-dir"))cron_dir=v;else if(!strcmp(k,"--crontab-bin"))cron_bin=v;else if(!strcmp(k,"--run-schedule"))run_id=atoi(v);else {fprintf(stderr,"Unknown option %s\n",k);return 2;}
 }
 if(!data_arg||port<1024||port>65535|| (strcmp(mode,"demo")&&strcmp(mode,"commands"))){fprintf(stderr,"Usage: zigma-agent --data-dir /absolute/path --web-dir /assets [--mode demo|commands] [--port 8765]\n");return 2;}
 if(strlen(data_arg)>=sizeof(data_dir)||!safe_path(data_arg)){fprintf(stderr,"Data directory requires an absolute shell-safe path\n");return 2;}snprintf(data_dir,sizeof(data_dir),"%s",data_arg);
 setenv("TZ","Europe/Madrid",1);tzset();umask(077);
 struct in_addr checked_bind;if(inet_pton(AF_INET,bind_ip,&checked_bind)!=1){fprintf(stderr,"IPv4 listen address required\n");return 2;}
 if(run_id)return cron_client(run_id);
 char *resolved_web=web_arg?realpath(web_arg,NULL):NULL;
 if(!resolved_web||strlen(resolved_web)>=sizeof(web_dir)){fprintf(stderr,"Valid --web-dir required\n");free(resolved_web);return 2;}
 strcpy(web_dir,resolved_web);free(resolved_web);
 if(!safe_path(release_file)){fprintf(stderr,"Invalid release manifest path\n");return 2;}
 if(core_records_dir&&!safe_path(core_records_dir)){fprintf(stderr,"Invalid core records path\n");return 2;}
 if(cron_helper&&(!safe_path(cron_helper)||access(cron_helper,X_OK))){fprintf(stderr,"Invalid cron helper\n");return 2;}
 if(cron_dir&&(!strcmp(mode,"demo")||!control_bin||!status_file||!safe_path(cron_dir)||!safe_path(cron_bin))){fprintf(stderr,"Real cron requires commands mode, explicit control/status adapters and trusted paths\n");return 2;}
 if(zones_bin&&(!strcmp(mode,"demo")||!map_file||!status_file||!control_bin||!safe_path(zones_bin)||access(zones_bin,X_OK))){fprintf(stderr,"Zone adapter requires commands mode, map/status/control adapters and valid helper\n");return 2;}
 if(area_bin&&(!strcmp(mode,"demo")||!map_file||!status_file||!control_bin||!safe_path(area_bin)||access(area_bin,X_OK))){fprintf(stderr,"Area adapter requires commands mode, map/status/control adapters and a valid helper\n");return 2;}
 if(map_library_dir&&(!maps_bin||!safe_path(map_library_dir)||access(map_library_dir,R_OK|W_OK))){fprintf(stderr,"Invalid master map library directory\n");return 2;}
 if(maps_app_pid&&(!maps_bin||!saved_id(maps_app_pid))){fprintf(stderr,"Invalid map app process\n");return 2;}
 if((maps_bin||maps_dir)&&(!maps_bin||!maps_dir||!safe_path(maps_bin)||!safe_path(maps_dir)||access(maps_bin,X_OK))){fprintf(stderr,"Saved maps require a valid helper and directory\n");return 2;}
 if((telegram_curl&&(!safe_path(telegram_curl)||access(telegram_curl,X_OK)))||(telegram_ca&&(!safe_path(telegram_ca)||access(telegram_ca,R_OK)))){fprintf(stderr,"Invalid Telegram transport paths\n");return 2;}
 if(settings_bin&&(!safe_path(settings_bin)||access(settings_bin,X_OK))){fprintf(stderr,"Invalid settings helper\n");return 2;}
 if(control_bin&&(!safe_path(control_bin)||access(control_bin,X_OK))){fprintf(stderr,"Invalid control helper\n");return 2;}
 if(mkdir(data_dir,0700)&&errno!=EEXIST)fatal("mkdir");
 char p[1024];path(p,sizeof(p),"agent.lock");int lock=open(p,O_RDWR|O_CREAT|O_CLOEXEC,0600);if(lock<0||flock(lock,LOCK_EX|LOCK_NB))fatal("agent lock");
 ssize_t executable_len=readlink("/proc/self/exe",executable,sizeof(executable)-1);
 if(executable_len<0||executable_len>=(ssize_t)sizeof(executable)-1||!safe_path(executable))fatal("executable path");
 path(p,sizeof(p),"access.token");char *t=read_file(p,128);
 if(t){t[strcspn(t,"\r\n")]=0;if(!access_key_valid(t)){fprintf(stderr,"Invalid access.token\n");return 2;}snprintf(token,sizeof(token),"%s",t);free(t);}else{
  unsigned char raw[24];int r=open("/dev/urandom",O_RDONLY);if(r<0||read(r,raw,sizeof(raw))!=(ssize_t)sizeof(raw))fatal("random");close(r);for(size_t i=0;i<sizeof(raw);i++)snprintf(token+2*i,3,"%02x",raw[i]);if(!atomic_write(p,token))fatal("access token");}
 path(p,sizeof(p),"state.json");char *s=read_file(p,256*1024);if(s){state=strict_json(s);free(s);if(!state||!cJSON_IsArray(get(state,"schedules"))||!cJSON_IsArray(get(state,"events"))||!cJSON_IsArray(get(state,"zones"))){fprintf(stderr,"Invalid state file; refusing to replace it\n");return 2;}}
 else{state=cJSON_CreateObject();cJSON_AddArrayToObject(state,"schedules");cJSON_AddArrayToObject(state,"events");cJSON_AddArrayToObject(state,"zones");setnum(state,"next_schedule_id",1);setnum(state,"sequence",0);setbool(state,"cron_applied",false);event("system","Local agent started");if(!save_state())fatal("state");}
 migrate_event_language();
 cJSON *initial_settings=settings_json();cJSON_Delete(initial_settings);
 char err[200];if(!sync_cron(err,sizeof(err))){fprintf(stderr,"Cron: %s\n",err);setbool(state,"cron_applied",false);}else setbool(state,"cron_applied",cron_dir!=NULL);if(!save_state())fatal("state");
 signal(SIGINT,stop);signal(SIGTERM,stop);signal(SIGPIPE,SIG_IGN);listener=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);if(listener<0)fatal("socket");int one=1;setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons((unsigned short)port)};if(inet_pton(AF_INET,bind_ip,&a.sin_addr)!=1){fprintf(stderr,"IPv4 listen address required\n");return 2;}
 if(bind(listener,(struct sockaddr*)&a,sizeof(a))||listen(listener,8))fatal("listen");
 printf("Zigma Free · %s · %s\nhttp://%s:%d\n",robot,mode,bind_ip,port);fflush(stdout);
 while(running){if(nor_pid>0&&waitpid(nor_pid,NULL,WNOHANG)==nor_pid)nor_pid=0;tg_tick();fd_set rd;FD_ZERO(&rd);FD_SET(listener,&rd);struct timeval tv={1,0};if(select(listener+1,&rd,NULL,NULL,&tv)<=0)continue;int fd=accept(listener,NULL,NULL);if(fd<0)continue;if(fcntl(fd,F_SETFD,FD_CLOEXEC)){close(fd);continue;}struct timeval timeout={3,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));handle(fd);close(fd);}
 tg_stop();close(listener);cJSON_Delete(state);close(lock);return 0;
}
