#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#define time fake_time
#define clock_gettime fake_clock_gettime
#define sleep fake_sleep
#define main control_main
#define msgget fake_msgget
#define msgsnd fake_msgsnd
#include "../src/zigmactl.c"
#undef main
static time_t tick=1000;static const char *fixture_status;static int scenario;
time_t fake_time(time_t *out){if(out)*out=tick;return tick;}
int fake_clock_gettime(clockid_t id,struct timespec *out){(void)id;out->tv_sec=tick;out->tv_nsec=0;return 0;}
unsigned int fake_sleep(unsigned int seconds){
 tick+=seconds;
 if(fixture_status){FILE *f=fopen(fixture_status,"w");assert(f);
 const char *w=scenario==3?(tick<1003?"Hibernating":tick<1006?"PlanningLocation":"Standby"):
 scenario==4?(tick<1003?"Hibernating":tick<1006?"BackCharging":"PileCharging"):
 scenario!=1?(tick<1006?"PlanningLocation":"Standby"):"Standby";
 time_t sample=scenario==2?tick-tick%3:tick;
 fprintf(f,"{\"working_status\":\"%s\",\"error\":\"NoError\",\"cache_read_at\":%ld,\"local_link_received_at\":%ld,\"navigation_pose\":{\"map_id\":\"a\",\"width\":100,\"height\":100,\"map_origin\":[40,50],\"x\":60,\"y\":80,\"cache_read_at\":%ld}}",w,(long)sample,(long)sample,(long)sample);fclose(f);}
 return 0;
}
static const char *send_log;
static int sends,fan_pauses,fail_fan;static long sent_type;static char sent_json[4096],first_json[4096];
int fake_msgget(key_t key,int flags){(void)key;assert(flags==0);return 9;}
int fake_msgsnd(int q,const void *p,size_t n,int flags){assert(q==9&&n==4096&&flags==IPC_NOWAIT);sent_type=*(const long*)p;memcpy(sent_json,(const char*)p+sizeof(long),4096);sends++;if(send_log){FILE *f=fopen(send_log,"a");assert(f);fprintf(f,"%s\n",sent_json);fclose(f);}if(sends==1)memcpy(first_json,sent_json,4096);if(strstr(sent_json,"\"FanStatus\":\"Pause\"")){fan_pauses++;if(fail_fan){errno=EAGAIN;return -1;}}return 0;}
int main(void){
 cJSON *m=cJSON_Parse("{\"available\":true,\"map_id\":\"a\",\"width\":100,\"height\":100,\"map_origin\":[40,50]}");
 assert(nav_map(m,20,30));assert(!nav_map(m,60,30));assert(nav_map(m,-40,-50));assert(!nav_map(m,-41,0));
 int n;assert(nav_int("-10",-20000,20000,&n)&&n==-10);assert(!nav_int("nan",-20000,20000,&n));assert(!nav_int("1.5",-20000,20000,&n));
 cJSON *s=cJSON_Parse("{\"working_status\":\"Standby\",\"error\":\"NoError\"}");
 cJSON_AddNumberToObject(s,"cache_read_at",time(NULL));cJSON_AddNumberToObject(s,"local_link_received_at",time(NULL));
 cJSON *p=cJSON_Duplicate(m,1);cJSON_AddNumberToObject(p,"cache_read_at",time(NULL));cJSON_AddNumberToObject(p,"x",60);cJSON_AddNumberToObject(p,"y",80);cJSON_AddItemToObject(s,"navigation_pose",p);
 bool started=false;assert(nav_progress(s,m,20,30,false,&started)==0); /* stale idle cannot complete */
 cJSON_SetValuestring(nv(s,"working_status"),"PlanningLocation");assert(nav_progress(s,m,20,30,false,&started)==0&&started);
 cJSON_SetValuestring(nv(s,"working_status"),"Standby");assert(nav_progress(s,m,20,30,false,&started)==1);
 cJSON_SetNumberValue(nv(p,"x"),70);assert(nav_progress(s,m,20,30,false,&started)==-5);
 cJSON_SetNumberValue(nv(p,"x"),60);cJSON_SetValuestring(nv(p,"map_id"),"b");assert(nav_progress(s,m,20,30,false,&started)==-3);
 cJSON_SetValuestring(nv(p,"map_id"),"a");cJSON_SetValuestring(nv(s,"error"),"WheelSuspendInMidair");assert(nav_progress(s,m,20,30,false,&started)==-5);
 cJSON_SetValuestring(nv(s,"error"),"NoError");cJSON_SetNumberValue(nv(s,"local_link_received_at"),time(NULL)-20);assert(nav_progress(s,m,20,30,false,&started)==-3);
 cJSON_SetNumberValue(nv(s,"local_link_received_at"),time(NULL));started=false;
 cJSON_SetValuestring(nv(s,"working_status"),"BackCharging");assert(nav_progress(s,NULL,0,0,true,&started)==0&&started);
 cJSON_SetValuestring(nv(s,"working_status"),"DirCharging");assert(nav_progress(s,NULL,0,0,true,&started)==0); /* wired charging is not docking */
 cJSON_SetValuestring(nv(s,"working_status"),"PileCharging");assert(nav_progress(s,NULL,0,0,true,&started)==1);
 cJSON_SetValuestring(nv(s,"working_status"),"Hibernating");assert(nav_idle(s));
 cJSON_SetNumberValue(nv(s,"local_link_received_at"),time(NULL)-20);assert(!nav_link(s));
 cJSON_SetNumberValue(nv(s,"local_link_received_at"),time(NULL));
 started=false;assert(nav_progress(s,m,20,30,false,&started)==0&&!started); /* sleep is not arrival */
 cJSON_SetValuestring(nv(s,"working_status"),"AutoClean");assert(!nav_idle(s));
 cJSON_SetValuestring(nv(s,"working_status"),"Malfunction");assert(!nav_idle(s));
 const char *json="{\"FanStatus\":\"0\",\"goto_point\":[20,30]}";assert(nav_json(1,json)==0&&sent_type==1&&!strcmp(sent_json,json));
 char *bad[]={"zigmactl","goto","10","20","unexpected",NULL};sends=0;assert(control_main(5,bad)==2&&sends==0);
 char dir[]="/tmp/navigation-test-XXXXXX";assert(mkdtemp(dir));char status[256],map[256],lock[256],state[256];
 snprintf(status,sizeof status,"%s/status",dir);snprintf(map,sizeof map,"%s/map",dir);snprintf(lock,sizeof lock,"%s/lock",dir);snprintf(state,sizeof state,"%s/state",dir);
 setenv("ZIGMA_STATUS_FILE",status,1);setenv("ZIGMA_MAP_FILE",map,1);setenv("ZIGMA_NAV_LOCK",lock,1);setenv("ZIGMA_STATE_FILE",state,1);
 char *text=cJSON_PrintUnformatted(m);FILE *f=fopen(map,"w");assert(f);fputs(text,f);fclose(f);free(text);
 fixture_status=status;scenario=1;tick=1000;fake_sleep(0);scenario=0;sends=0;
 assert(nav_move(20,30,false,true,30)==0&&sends==2&&fan_pauses==1&&tick==1007);
 assert(sent_type==1&&strstr(first_json,"\"goto_point\":[20,30]")&&strstr(first_json,"\"FanStatus\":\"0\""));
 tick=1000;scenario=1;fake_sleep(0);scenario=2;sends=0;
 assert(nav_move(20,30,false,true,30)==0&&sends==2&&tick==1011); /* two distinct observer samples, 3s cadence */
 tick=1000;scenario=1;fake_sleep(0);sends=0;
 assert(nav_move(20,30,false,true,10)==4&&sends==2); /* no active transition: timeout and pause */
 tick=1000;scenario=1;fake_sleep(0);scenario=0;sends=0;fan_pauses=0;
 assert(nav_watch_start(m,map,1000)==0&&sends==1&&fan_pauses==1&&tick==1001);
 tick=1000;scenario=1;fake_sleep(0);sends=0;
 assert(nav_watch_start(m,map,1000)==4&&sends==0); /* never apply to old idle */
 tick=1000;scenario=1;fake_sleep(0);scenario=0;sends=0;fail_fan=1;
 assert(nav_move(20,30,false,true,30)==1&&sends==3); /* goto, failed fan Pause, motion pause */
 fail_fan=0;
 tick=1000;scenario=3;fake_sleep(0);sends=0;fan_pauses=0;
 assert(nav_move(20,30,false,true,30)==0&&sends==2&&fan_pauses==1); /* start from sleep, then active+arrival */
 tick=1000;scenario=3;fake_sleep(0);sends=0;
 FILE *stale=fopen(status,"w");assert(stale);fputs("{\"working_status\":\"Hibernating\",\"error\":\"NoError\",\"cache_read_at\":1000,\"local_link_received_at\":980}",stale);fclose(stale);
 assert(nav_move(20,30,false,true,30)==3&&sends==0); /* sleeping never bypasses stale link */
 stale=fopen(status,"w");assert(stale);fputs("{\"working_status\":\"Hibernating\",\"error\":\"VendorError26\",\"cache_read_at\":1000,\"local_link_received_at\":1000}",stale);fclose(stale);
 assert(nav_move(20,30,false,true,30)==3&&sends==0);
 tick=1000;scenario=4;fake_sleep(0);sends=0;
 assert(nav_move(0,0,true,true,30)==0&&sends==1); /* docking already accepts Hibernating */
 char journal[256],watchlog[256];snprintf(journal,sizeof journal,"%s/sends",dir);snprintf(watchlog,sizeof watchlog,"%s/watcher",dir);setenv("ZIGMA_NAV_LOG",watchlog,1);
 send_log=journal;tick=1000;scenario=1;fake_sleep(0);scenario=0;sends=0;
 assert(nav_move(20,30,false,false,30)==0&&sends==1&&tick==1000); /* parent returns without waiting */
 int child_status;assert(waitpid(-1,&child_status,0)>0&&WIFEXITED(child_status)&&WEXITSTATUS(child_status)==0);
 char records[1024]={0};f=fopen(journal,"r");assert(f);assert(fread(records,1,sizeof(records)-1,f)>0);fclose(f);
 assert(strstr(records,"\"goto_point\"")&&strstr(records,"\"FanStatus\":\"Pause\""));
 send_log=NULL;unlink(journal);unlink(watchlog);
 unlink(status);unlink(map);unlink(lock);rmdir(dir);
 cJSON_Delete(s);cJSON_Delete(m);return 0;
}
