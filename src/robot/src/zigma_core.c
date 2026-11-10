/* First standalone local receiver. No motion, provisioning, OTA or WAN transport. */
#define _POSIX_C_SOURCE 200809L
#include "zigma_version.h"
#include "zigma_local_ipc.h"
#include "cJSON.h"
#include "zigma_process.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>

static volatile sig_atomic_t stop_requested;
static const char *runtime="/tmp/zigma-core",*marker="/root/zigma-free/local-mode.lock";
static const char *ipc_path="/",*proc_root="/proc";
static const char *pairing_socket="/tmp/zigma-network/pairing.sock";
static unsigned long queue_count,map_count,dropped,invalid,unknown,history_count;
static unsigned long types[18];
static const char *last_kind="none";
static bool io_failed;
static cJSON *reported_status,*reported_settings,*reported_info;
static long long monotonic_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (long long)t.tv_sec*1000+t.tv_nsec/1000000;}
static void stop_signal(int sig){(void)sig;stop_requested=1;}
static bool path_join(char *out,size_t size,const char *name){int n=snprintf(out,size,"%s/%s",runtime,name);return n>0&&(size_t)n<size;}
static bool selected(void){struct stat s;return !lstat(marker,&s)&&S_ISREG(s.st_mode);}
static int rival(void){
 DIR *d=opendir(proc_root);if(!d)return -1;struct dirent *e;int result=0;
 while((e=readdir(d))){
  char *end;long pid=strtol(e->d_name,&end,10);if(!pid||*end)continue;
  char path[1024],name[64];int n=snprintf(path,sizeof path,"%s/%ld/comm",proc_root,pid);
  if(n<0||(size_t)n>=sizeof path){result=-1;break;}
  FILE *f=fopen(path,"r");if(!f){if(errno==ENOENT||errno==ESRCH)continue;result=-1;break;}
  bool ok=fgets(name,sizeof name,f)!=NULL;fclose(f);if(!ok){
   /* /proc entries can disappear after fopen while short-lived tasks exit. */
   if(!zigma_start_ticks((pid_t)pid))continue;
   result=-1;break;
  }
  name[strcspn(name,"\r\n")]=0;
  if(!strcmp(name,"ygclient")||!strcmp(name,"daemon")){result=1;break;}
 }
 closedir(d);return result;
}
static bool write_json(const char *name,cJSON *object){
 char path[1024],tmp[1040];if(!path_join(path,sizeof path,name))return false;
 int n=snprintf(tmp,sizeof tmp,"%s.new",path);if(n<0||(size_t)n>=sizeof tmp)return false;
 char *text=cJSON_PrintUnformatted(object);if(!text)return false;
 int fd=open(tmp,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);bool ok=false;
 if(fd>=0){FILE *f=fdopen(fd,"w");if(f){ok=fputs(text,f)>=0;if(fclose(f))ok=false;}else close(fd);}
 free(text);if(ok&&rename(tmp,path)==0)return true;unlink(tmp);return false;
}
static void output(const char *name,cJSON *object){if(!object||!write_json(name,object))io_failed=true;cJSON_Delete(object);}
static cJSON *get(const cJSON *o,const char *key){return cJSON_GetObjectItemCaseSensitive(o,key);}
static bool unique(const cJSON *o,unsigned depth){
 if(depth>32)return false;
 for(const cJSON *a=o->child;a;a=a->next){
  if(cJSON_IsObject(o))for(const cJSON *b=a->next;b;b=b->next)if(a->string&&b->string&&!strcmp(a->string,b->string))return false;
  if((cJSON_IsObject(a)||cJSON_IsArray(a))&&!unique(a,depth+1))return false;
 }
 return true;
}
static cJSON *parse(const char *text,size_t length){
 cJSON *o=cJSON_ParseWithLengthOpts(text,length+1,NULL,1);
 if(!cJSON_IsObject(o)||!unique(o,0)){cJSON_Delete(o);return NULL;}
 cJSON *version=get(o,"ProtocolVersion");
 if(version&&(!cJSON_IsString(version)||strcmp(version->valuestring,"1.0"))){cJSON_Delete(o);return NULL;}
 return o;
}
static void copy_fields(cJSON *out,const cJSON *in,const char *const *fields){
 for(size_t i=0;fields[i];i++){
  cJSON *v=get(in,fields[i]);if(v){cJSON *copy=cJSON_Duplicate(v,1);if(!copy||!cJSON_AddItemToObject(out,fields[i],copy)){cJSON_Delete(copy);io_failed=true;}}
 }
}
static void finite_number(cJSON *out,const cJSON *in,const char *src,const char *dst,double min,double max,double factor){
 const cJSON *v=get(in,src);
 if(v)cJSON_DeleteItemFromObjectCaseSensitive(out,dst);
 if(cJSON_IsNumber(v)&&isfinite(v->valuedouble)&&v->valuedouble>=min&&v->valuedouble<=max)cJSON_AddNumberToObject(out,dst,v->valuedouble*factor);
}
static const char *known_state(const cJSON *v){
 static const char *const states[]={"Standby","PileCharging","DirCharging","AutoClean","PlanningRect","SelectClean","SpotClean","Pause","BackCharging","Relocation","Hibernating","Malfunction","DirectionControl","PlanningLocation","LocationAlarm","CleanDone",NULL};
 if(cJSON_IsString(v))for(size_t i=0;states[i];i++)if(!strcmp(v->valuestring,states[i]))return states[i];
 return "unknown";
}
static cJSON *stamp(void){
 cJSON *o=cJSON_CreateObject();cJSON_AddNumberToObject(o,"received_at",(double)time(NULL));
 cJSON_AddNumberToObject(o,"received_monotonic_ms",(double)monotonic_ms());
 cJSON_AddNumberToObject(o,"receiver_pid",getpid());
 cJSON_AddNumberToObject(o,"receiver_start_ticks",(double)zigma_start_ticks(getpid()));
 cJSON_AddNullToObject(o,"measurement_time");return o;
}
#include "zigma_sessions.h"
#include "zigma_core_store.h"
static void session_projection(void){if(data_dir){
 cJSON *o=zs_catalog(data_dir),*old=zs_load(data_dir,"sessions.json",262144);
 if(!o||(!cJSON_Compare(o,old,1)&&!durable_write("sessions.json",o)))io_failed=true;
 cJSON_Delete(old);cJSON_Delete(o);
}}
static void status_message(const cJSON *in){
 cJSON *o=reported_status?cJSON_Duplicate(reported_status,1):cJSON_CreateObject();
 cJSON *meta=stamp();for(cJSON *v=meta->child;v;v=v->next){cJSON_DeleteItemFromObjectCaseSensitive(o,v->string);cJSON_AddItemToObject(o,v->string,cJSON_Duplicate(v,1));}cJSON_Delete(meta);
 cJSON_DeleteItemFromObjectCaseSensitive(o,"telemetry_source");cJSON_AddStringToObject(o,"telemetry_source","local_ipc_report");
 if(get(in,"WorkStatus")||!get(o,"working_status")){
  cJSON_DeleteItemFromObjectCaseSensitive(o,"working_status");cJSON_AddStringToObject(o,"working_status",known_state(get(in,"WorkStatus")));
 }
 cJSON *error=get(in,"ErrorInfo");if(error||!get(o,"error")){
  cJSON_DeleteItemFromObjectCaseSensitive(o,"error");cJSON_DeleteItemFromObjectCaseSensitive(o,"vendor_error_code");
  const char *name="unknown";char numeric[40];
  if(cJSON_IsString(error)&&strlen(error->valuestring)>0&&strlen(error->valuestring)<=64){
   bool safe=true;for(const char *p=error->valuestring;*p;p++)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_'))safe=false;
   if(safe)name=error->valuestring;
   if(!strcmp(name,"NoError"))cJSON_AddNumberToObject(o,"vendor_error_code",0);
  }else if(cJSON_IsNumber(error)&&isfinite(error->valuedouble)&&error->valuedouble>=0&&error->valuedouble<=65535&&floor(error->valuedouble)==error->valuedouble){
   cJSON_AddNumberToObject(o,"vendor_error_code",error->valuedouble);snprintf(numeric,sizeof numeric,"VendorError%d",error->valueint);name=error->valueint?numeric:"NoError";
  }
  cJSON_AddStringToObject(o,"error",name);
 }
 finite_number(o,in,"BatteryLevel","battery_level",0,100,1);
 finite_number(o,in,"CleanArea","clean_area_m2",0,100000,1);
 finite_number(o,in,"CleanTime","clean_time_s",0,1000000,60);
 cJSON_DeleteItemFromObjectCaseSensitive(o,"statistics_units_source");cJSON_AddStringToObject(o,"statistics_units_source","vendor_history_contract");
 /* A local report can contain cached state. Arrival is never a sample timestamp. */
 cJSON_DeleteItemFromObjectCaseSensitive(o,"fresh");cJSON_AddBoolToObject(o,"fresh",false);
 cJSON_Delete(reported_status);reported_status=cJSON_Duplicate(o,1);output("status.json",o);last_kind="status";
}
static cJSON *record_fields(const cJSON *in){
 cJSON *o=cJSON_CreateObject();const char *const dates[]={"CStartTime","CEndTime",NULL};
 for(unsigned i=0;dates[i];i++){const cJSON *v=get(in,dates[i]);if(zs_time(v))cJSON_AddItemToObject(o,dates[i],cJSON_Duplicate(v,1));}
 finite_number(o,in,"CleanArea","CleanArea",0,100000,1);finite_number(o,in,"CleanTime","CleanTime",0,1000000,1);
 const char *const flags[]={"CUploadRecord","CleanFinish",NULL};for(unsigned i=0;flags[i];i++){const cJSON *v=get(in,flags[i]);if(cJSON_IsBool(v))cJSON_AddBoolToObject(o,flags[i],cJSON_IsTrue(v));}
 const char *const ids[]={"map_id","HisMapId",NULL};for(unsigned i=0;ids[i];i++){const cJSON *v=get(in,ids[i]);if(cJSON_IsString(v)&&strlen(v->valuestring)<=64)cJSON_AddStringToObject(o,ids[i],v->valuestring);}
 return o;
}
static void record(const cJSON *in){
 cJSON *payload=record_fields(in),*o=stamp();for(cJSON *v=payload->child;v;v=v->next)cJSON_AddItemToObject(o,v->string,cJSON_Duplicate(v,1));
 archive_record("record",payload);cJSON_Delete(payload);output("last-record.json",o);history_count++;last_kind="record";
}
static void spatial(const cJSON *in){
 static const char *const fields[]={"MapWidth","MapHigh","MapOrigin","MapResolution","MapData","PointData","PointType","ChargerPoint","HisMapId","map_id","room_zone_info","forbidden_zone_info","laser_wall_line_x","laser_wall_line_y","planning_rect_x","planning_rect_y",NULL};
 cJSON *o=stamp();copy_fields(o,in,fields);cJSON *metadata=record_fields(in);
 for(cJSON *v=metadata->child;v;v=v->next){cJSON_DeleteItemFromObjectCaseSensitive(o,v->string);cJSON_AddItemToObject(o,v->string,cJSON_Duplicate(v,1));}
 if(cJSON_IsTrue(get(in,"CUploadRecord"))){
  cJSON *payload=cJSON_Duplicate(o,1);const char *const transient[]={"received_at","received_monotonic_ms","receiver_pid","receiver_start_ticks","measurement_time",NULL};for(unsigned i=0;transient[i];i++)cJSON_DeleteItemFromObjectCaseSensitive(payload,transient[i]);archive_record("spatial",payload);cJSON_Delete(payload);
  output("record-spatial.json",cJSON_Duplicate(o,1));
 }
 else if(cJSON_IsString(get(in,"MapData")))output("raster.json",cJSON_Duplicate(o,1));
 cJSON_Delete(metadata);output("spatial.json",o);last_kind="spatial";
}
static void metadata_message(const cJSON *in,bool settings){
 static const char *const option_strings[]={"FanStatus","WaterLevel",NULL};
 static const char *const info_strings[]={"DeviceType","FirmwareVersion","SystemVersion",NULL};
 const char *const *fields=settings?option_strings:info_strings;
 cJSON *o=stamp();unsigned accepted=0;
 for(unsigned i=0;fields[i];i++){
  cJSON *v=get(in,fields[i]);if(cJSON_IsString(v)&&strlen(v->valuestring)<=128){
   bool safe=true;for(const unsigned char *p=(unsigned char*)v->valuestring;*p;p++)if(*p<32||*p==127)safe=false;
   if(safe){cJSON_AddStringToObject(o,fields[i],v->valuestring);accepted++;}
  }
 }
 if(settings){
  cJSON *volume=get(in,"Volume");if(cJSON_IsNumber(volume)&&volume->valuedouble>=0&&volume->valuedouble<=100){cJSON_AddNumberToObject(o,"Volume",volume->valuedouble);accepted++;}
  static const char *const flags[]={"over_ridge","ContinueCleaning",NULL};
  for(unsigned i=0;flags[i];i++){cJSON *v=get(in,flags[i]);if(cJSON_IsBool(v)){cJSON_AddBoolToObject(o,flags[i],cJSON_IsTrue(v));accepted++;}}
 }
 if(accepted){
  cJSON **cache=settings?&reported_settings:&reported_info;cJSON *merged=*cache?cJSON_Duplicate(*cache,1):cJSON_CreateObject();
  for(cJSON *v=o->child;v;v=v->next){cJSON_DeleteItemFromObjectCaseSensitive(merged,v->string);cJSON_AddItemToObject(merged,v->string,cJSON_Duplicate(v,1));}
  cJSON_Delete(o);cJSON_Delete(*cache);*cache=cJSON_Duplicate(merged,1);output(settings?"settings.json":"device-info.json",merged);last_kind=settings?"settings":"device_info";
 }
 else{cJSON_Delete(o);unknown++;last_kind="unhandled";}
}
static long long pairing_pending;
static bool request_pairing(void){
 struct sockaddr_un a={.sun_family=AF_UNIX};if(strlen(pairing_socket)>=sizeof a.sun_path)return false;
 memcpy(a.sun_path,pairing_socket,strlen(pairing_socket)+1);
 int fd=socket(AF_UNIX,SOCK_DGRAM,0);if(fd<0)return false;
 fcntl(fd,F_SETFD,FD_CLOEXEC);fcntl(fd,F_SETFL,O_NONBLOCK);
 char request[64];int n=snprintf(request,sizeof request,"PAIR %lld",monotonic_ms());
 bool ok=sendto(fd,request,(size_t)n,0,(struct sockaddr*)&a,sizeof a)==n;close(fd);return ok;
}
static void handle(long type,const char *text,size_t length,bool from_map){
 cJSON *in=parse(text,length);if(!in){invalid++;return;}
 if(from_map){
  if(cJSON_IsTrue(get(in,"CUploadRecord")))record(in);
  spatial(in);if(cJSON_IsTrue(get(in,"CUploadRecord")))session_projection();
 }else if(type==1&&cJSON_IsString(get(in,"SoftAPMode"))&&!strcmp(get(in,"SoftAPMode")->valuestring,"On")&&!get(in,"FactoryReset")){
  /* Only the physical Wi-Fi event. Never forward factory-reset/cloud payloads. */
  if(request_pairing()){last_kind="pairing_requested";pairing_pending=monotonic_ms();}else{unknown++;last_kind="pairing_unavailable";}
 }else if(type==2){status_message(in);}
 else if(type==4||type==8){metadata_message(in,type==4);}
 else if(type==12){
  static const char *const commands[]={"virtual_wall_and_forbidden_set","local_map_operation","selected_zone_clean",NULL};
  cJSON *o=stamp();const cJSON *code=get(in,"result_code");
  if(cJSON_IsNumber(code)&&isfinite(code->valuedouble)&&floor(code->valuedouble)==code->valuedouble&&fabs(code->valuedouble)<=100000)cJSON_AddNumberToObject(o,"result_code",code->valuedouble);
  cJSON *cmd=get(in,"rsp_cmd");bool known=false;
  if(cJSON_IsString(cmd))for(unsigned i=0;commands[i];i++)if(!strcmp(cmd->valuestring,commands[i])){cJSON_AddStringToObject(o,"rsp_cmd",cmd->valuestring);known=true;break;}
  cJSON_AddStringToObject(o,"correlation","unattributed");cJSON_AddBoolToObject(o,"applied_confirmed",false);
  /* Geometry is confirmed through the independent returned map, not arbitrary
   * response data. No opaque nested payload is retained. */
  cJSON_AddBoolToObject(o,"recognized_command",known);
  output("last-response.json",o);last_kind="response";
 }else{
  /* Includes provisioning/OTA/control: drain without executing or storing payload. */
  unknown++;last_kind="unhandled";
 }
 cJSON_Delete(in);
}
static void health(const char *phase){
 cJSON *o=cJSON_CreateObject();cJSON_AddStringToObject(o,"phase",phase);
 cJSON_AddNumberToObject(o,"pid",getpid());cJSON_AddNumberToObject(o,"updated_at",(double)time(NULL));
 cJSON_AddNumberToObject(o,"start_ticks",(double)zigma_start_ticks(getpid()));
 cJSON_AddNumberToObject(o,"monotonic_ms",(double)monotonic_ms());
 cJSON_AddNumberToObject(o,"queue_messages",queue_count);cJSON_AddNumberToObject(o,"map_messages",map_count);
 cJSON_AddNumberToObject(o,"dropped",dropped);cJSON_AddNumberToObject(o,"invalid",invalid);
 cJSON_AddNumberToObject(o,"unhandled",unknown);cJSON_AddNumberToObject(o,"records",history_count);
 cJSON_AddStringToObject(o,"last_kind",last_kind);cJSON *counts=cJSON_AddObjectToObject(o,"message_types");
 for(unsigned i=1;i<18;i++)if(types[i]){char key[16];snprintf(key,sizeof key,"%u",i);cJSON_AddNumberToObject(counts,key,types[i]);}
 output("health.json",o);
}
/* Read-only liveness check shared by the coordinator, with no IPC attachment. */
static int check_health(const char *path,const char *exe){
 int fd=open(path,O_RDONLY|O_NOFOLLOW);if(fd<0)return 1;struct stat st;
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||st.st_size>32768){close(fd);return 1;}
 char raw[32769];size_t off=0;while(off<(size_t)st.st_size){ssize_t n=read(fd,raw+off,(size_t)st.st_size-off);if(n<0&&errno==EINTR)continue;if(n<=0)break;off+=(size_t)n;}close(fd);raw[off]=0;
 cJSON *o=off==(size_t)st.st_size?parse(raw,off):NULL;if(!o)return 1;
 cJSON *pid=get(o,"pid"),*ticks=get(o,"start_ticks"),*seen=get(o,"monotonic_ms"),*phase=get(o,"phase");
 if(!pid){pid=get(o,"manager_pid");ticks=get(o,"manager_start");phase=get(o,"state");}
 if(!pid){pid=get(o,"observer_pid");ticks=get(o,"observer_start_ticks");seen=get(o,"observer_monotonic_ms");cJSON_AddStringToObject(o,"phase","observing");phase=get(o,"phase");}
 bool ok=cJSON_IsNumber(pid)&&isfinite(pid->valuedouble)&&pid->valuedouble>=1&&pid->valuedouble<=2147483647&&floor(pid->valuedouble)==pid->valuedouble&&cJSON_IsNumber(ticks)&&isfinite(ticks->valuedouble)&&ticks->valuedouble>0&&ticks->valuedouble<1e16&&cJSON_IsNumber(seen)&&isfinite(seen->valuedouble)&&monotonic_ms()>=seen->valuedouble&&monotonic_ms()-seen->valuedouble<=20000&&cJSON_IsString(phase);
 if(ok){
  const char *bad[]={"stopped","failed","ownership_conflict","process_scan_failed",NULL};for(unsigned i=0;bad[i];i++)if(!strcmp(phase->valuestring,bad[i]))ok=false;
  char proc[64],actual[1024]={0};snprintf(proc,sizeof proc,"/proc/%d/exe",pid->valueint);
  if(readlink(proc,actual,sizeof actual-1)<=0||strcmp(actual,exe)||zigma_start_ticks(pid->valueint)!=(unsigned long long)ticks->valuedouble)ok=false;
 }
 cJSON_Delete(o);return ok?0:1;
}
/* Complete the native Wi-Fi indicator/voice flow after local LAN recovery.
 * The Connected message is the existing local handshake, never a cloud claim. */
static void pairing_feedback(struct zlocal_ipc *ipc){
 static bool initial=true;if(!initial&&!pairing_pending)return;initial=false;
 char path[1024];snprintf(path,sizeof path,"%s",pairing_socket);char *slash=strrchr(path,'/');if(!slash)return;
 snprintf(slash+1,(size_t)(path+sizeof path-slash-1),"status.json");
 int fd=open(path,O_RDONLY|O_NOFOLLOW);if(fd<0)return;
 char raw[2048];ssize_t n=read(fd,raw,sizeof raw-1);close(fd);if(n<=0||n==(ssize_t)sizeof raw-1)return;raw[n]=0;
 cJSON *o=parse(raw,(size_t)n);if(!o)return;
 cJSON *state=get(o,"state"),*seen=get(o,"monotonic_ms"),*pid=get(o,"manager_pid"),*ticks=get(o,"manager_start");
 bool live=cJSON_IsString(state)&&cJSON_IsNumber(seen)&&isfinite(seen->valuedouble)&&
  seen->valuedouble<=monotonic_ms()&&monotonic_ms()-seen->valuedouble<20000&&
  cJSON_IsNumber(pid)&&pid->valuedouble>0&&pid->valuedouble<2147483647&&cJSON_IsNumber(ticks)&&
  ticks->valuedouble>0&&ticks->valuedouble==(double)zigma_start_ticks((pid_t)pid->valueint);
 if(live){
  if(!strcmp(state->valuestring,"pairing_ap")||!strcmp(state->valuestring,"pairing_connecting")){
   if(!pairing_pending)pairing_pending=(long long)seen->valuedouble;
  }else if(pairing_pending&&seen->valuedouble>pairing_pending&&!strcmp(state->valuestring,"lan_addressed")){
   const char message[]="{\"ProtocolVersion\":\"1.0\",\"Connected\":true}";
   if(zlocal_send(ipc,2,message,strlen(message))==ZLOCAL_READY)pairing_pending=0;
  }
 }
 cJSON_Delete(o);
}
static int check_web(const char *value){
 char *end;long port=strtol(value,&end,10);if(!*value||*end||port<1024||port>65535)return 2;
 int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return 1;
 struct timeval timeout={2,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);
 struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons((unsigned short)port)};inet_pton(AF_INET,"127.0.0.1",&a.sin_addr);
 bool ok=connect(fd,(struct sockaddr*)&a,sizeof a)==0;
 char request[128];int length=snprintf(request,sizeof request,"GET / HTTP/1.1\r\nHost: 127.0.0.1:%ld\r\nConnection: close\r\n\r\n",port);
 char response[64]={0};if(ok)ok=send(fd,request,(size_t)length,MSG_NOSIGNAL)==length;
 size_t used=0;
 while(ok&&used<12){ssize_t n=recv(fd,response+used,sizeof response-1-used,0);if(n<=0){ok=false;break;}used+=(size_t)n;}
 if(ok)ok=!strncmp(response,"HTTP/1.1 200",12);
 close(fd);return ok?0:1;
}
int main(int argc,char **argv){
 if(zigma_version_only(argc,argv,"zigma-core-arm"))return 0;
 if(argc==3&&!strcmp(argv[1],"--check-web"))return check_web(argv[2]);
 if(argc==5&&!strcmp(argv[1],"--check-health")&&!strcmp(argv[3],"--check-exe"))return argv[2][0]=='/'&&argv[4][0]=='/'?check_health(argv[2],argv[4]):2;
 bool run=false,handshake=false,map_once=false;
 for(int i=1;i<argc;i++){
  if(!strcmp(argv[i],"--run")){run=true;continue;}
  if(!strcmp(argv[i],"--request-map-once")){map_once=true;continue;}
  if(!strcmp(argv[i],"--handshake")){handshake=true;continue;}
  if(i+1==argc)return 2;
  const char *key=argv[i],*value=argv[++i];if(value[0]!='/')return 2;
  if(!strcmp(key,"--runtime"))runtime=value;else if(!strcmp(key,"--marker"))marker=value;
  else if(!strcmp(key,"--data-dir"))data_dir=value;
  else if(!strcmp(key,"--pairing-socket"))pairing_socket=value;
  else if(!strcmp(key,"--ftok-path"))ipc_path=value;else if(!strcmp(key,"--proc-root"))proc_root=value;else return 2;
 }
 if(!run||!selected())return 2;
 umask(077);if(mkdir(runtime,0700)&&errno!=EEXIST)return 1;
 struct stat st;if(lstat(runtime,&st)||!S_ISDIR(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&0077))return 1;
 char path[1024];if(!path_join(path,sizeof path,"receiver.lock"))return 1;
 int lock=open(path,O_RDWR|O_CREAT|O_NOFOLLOW,0600);
 if(lock<0||flock(lock,LOCK_EX|LOCK_NB)){if(lock>=0)close(lock);return 1;}
 if(!store_open()){close(lock);return 1;}
 session_projection();if(io_failed){close(lock);return 1;}
 if(rival()!=0){health("ownership_conflict");close(lock);return 1;}
 struct sigaction sa={0};sa.sa_handler=stop_signal;sigemptyset(&sa.sa_mask);sigaction(SIGTERM,&sa,NULL);sigaction(SIGINT,&sa,NULL);
 struct zlocal_ipc ipc;if(zlocal_attach_keys(&ipc,ipc_path)){health("ipc_unavailable");close(lock);return 1;}
 if(rival()!=0||!selected()){zlocal_detach(&ipc);close(lock);return 1;}
 if(handshake){
  const char request[]="{\"ProtocolVersion\":\"1.0\",\"Connected\":true}";
  if(zlocal_send(&ipc,2,request,strlen(request))!=ZLOCAL_READY){health("handshake_failed");zlocal_detach(&ipc);close(lock);return 1;}
 }
 if(map_once){
  const char request[]="{\"ProtocolVersion\":\"1.0\",\"send_map_flag\":true}";
  if(zlocal_send(&ipc,2,request,strlen(request))!=ZLOCAL_READY){health("map_request_failed");zlocal_detach(&ipc);close(lock);return 1;}
 }
 char queue[ZLOCAL_MESSAGE_MAX+1];char *map=malloc(ZLOCAL_MAP_BYTES+1);
 if(!map){zlocal_detach(&ipc);close(lock);return 1;}
 bool failed=false;long long last_health=0;
 while(!stop_requested&&selected()){
  if(monotonic_ms()-last_health>=1000){
   int owner=rival();if(owner!=0){failed=true;last_kind=owner>0?"ownership_conflict":"process_inventory_error";break;}
   pairing_feedback(&ipc);health("receiving");last_health=monotonic_ms();
  }
  for(unsigned budget=0;budget<32&&!stop_requested;budget++){
   size_t length;long type;enum zlocal_result r=zlocal_receive(&ipc,&type,queue,sizeof queue,&length);
   if(r==ZLOCAL_ERROR){failed=true;break;}if(r==ZLOCAL_EMPTY)break;
   if(r==ZLOCAL_DROPPED){dropped++;continue;}
   queue_count++;if(type>0&&type<18)types[type]++;handle(type,queue,length,false);
  }
  if(failed)break;
  size_t length;enum zlocal_result r=zlocal_receive_map(&ipc,map,ZLOCAL_MAP_BYTES+1,&length);
  if(r==ZLOCAL_ERROR){failed=true;break;}if(r==ZLOCAL_DROPPED)dropped++;
  if(r==ZLOCAL_READY){map_count++;handle(0,map,length,true);}
  if(io_failed){failed=true;break;}
  struct timespec delay={0,100000000};nanosleep(&delay,NULL);
 }
 free(map);cJSON_Delete(reported_status);cJSON_Delete(reported_settings);cJSON_Delete(reported_info);zlocal_detach(&ipc);health(failed?"failed":"stopped");close(lock);
 return failed||io_failed?1:0;
}
