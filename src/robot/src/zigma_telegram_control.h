/* One dedicated bot per robot. Private chats; no LAN discovery or routing.
 * Persist each update before dispatch; uncertain physical actions are not retried. */
#include <ifaddrs.h>
#include <net/if.h>
#include "mbedtls/sha256.h"
static pid_t tg_reboot_pid;
static char tg_control_token[128];
static long long tg_seen,tg_confirmed;
static time_t tg_control_since,tg_last_update_at;
static cJSON *tg_inbox;
static bool tg_journal_failed,tg_control_opened;
static double tg_number(cJSON *o,const char *key){cJSON *v=get(o,key);return cJSON_IsNumber(v)?v->valuedouble:0;}
static bool tg_control_ready(void){return cJSON_IsTrue(get(tg_config,"commands"))&&*str(tg_config,"chat_id","");}
static bool tg_control_wanted(void){return tg_token_valid(str(tg_config,"token",""))&&(tg_control_ready()||tg_expires>time(NULL));}
static void tg_hmac(const char *key,const char *text,char out[65]){
 unsigned char k[64]={0},inner[32],mac[32];size_t n=strlen(key);if(n>64)mbedtls_sha256((const unsigned char*)key,n,k,0);else memcpy(k,key,n);
 unsigned char pad[64];mbedtls_sha256_context ctx;mbedtls_sha256_init(&ctx);
 for(int i=0;i<64;i++)pad[i]=k[i]^0x36;
 mbedtls_sha256_starts(&ctx,0);mbedtls_sha256_update(&ctx,pad,64);mbedtls_sha256_update(&ctx,(const unsigned char*)text,strlen(text));mbedtls_sha256_finish(&ctx,inner);
 for(int i=0;i<64;i++)pad[i]=k[i]^0x5c;
 mbedtls_sha256_starts(&ctx,0);mbedtls_sha256_update(&ctx,pad,64);mbedtls_sha256_update(&ctx,inner,32);mbedtls_sha256_finish(&ctx,mac);mbedtls_sha256_free(&ctx);
 for(int i=0;i<32;i++)snprintf(out+2*i,3,"%02x",mac[i]);
}
static bool tg_journal(void){
 char file[1024];path(file,sizeof file,"telegram-control.json");cJSON *o=cJSON_CreateObject();setnum(o,"seen",tg_seen);setnum(o,"confirmed",tg_confirmed);setnum(o,"last_update_at",tg_last_update_at);setstr(o,"token_tag","");
 char tag[65];tg_hmac(str(tg_config,"token",""),"Zigma Free control identity",tag);setstr(o,"token_tag",tag);
 char *s=cJSON_PrintUnformatted(o);bool ok=s&&atomic_write(file,s);free(s);cJSON_Delete(o);if(!ok){tg_journal_failed=true;snprintf(tg_result,sizeof tg_result,"Command journal unavailable; control stopped");}return ok;
}
static void tg_reply(const char *text){tg_enqueue(text,0);}
#include "zigma_telegram_status_song.h"
/* One bounded follow-up for an explicit action, independent of activity alerts. */
static time_t tg_action_at;
static bool tg_action_dock;
static void tg_command_feedback(cJSON *s,time_t now){
 if(!tg_action_at)return;
 time_t observed=(time_t)num(s,"observed_at",0),cache=(time_t)num(s,"cache_read_at",0),link=(time_t)num(s,"local_link_received_at",0);
 bool fresh=(observed>tg_action_at&&observed<=now&&now-observed<=30)||(cache>tg_action_at&&link>tg_action_at&&cache<=now&&link<=now&&now-cache<=15&&now-link<=15);
 const char *work=str(s,"working_status","");const char *reply=NULL;char error[160];
 cf_view(s);cJSON *feedback=get(s,"command_feedback");
 if(num(feedback,"dispatched_at",0)>=tg_action_at&&(!strcmp(str(feedback,"action",""),tg_action_dock?"dock":"clean")||(!tg_action_dock&&!strcmp(str(feedback,"action",""),"resume")))){
  const char *phase=str(feedback,"phase","");
  if(!strcmp(phase,"expected_state_reported"))reply="Robot reported the requested state after dispatch.";
  else if(!strcmp(phase,"error_reported"))reply="Robot reported an error after dispatch. Check the web.";
 }

 if(fresh&&!reply){
  if(num(s,"vendor_error_code",0)>0){snprintf(error,sizeof error,"Robot reports an error (code %d) after the command.",num(s,"vendor_error_code",0));reply=error;}
  else if(tg_action_dock&&(!strcmp(work,"PileCharging")||!strcmp(work,"DirCharging")))reply="Confirmed: robot is charging.";
  else if(tg_action_dock&&!strcmp(work,"BackCharging"))reply="Confirmed: returning to the dock.";
  else if(!tg_action_dock&&(!strcmp(work,"AutoClean")||!strcmp(work,"SpotClean")||!strcmp(work,"SelectClean")||!strcmp(work,"PlanningRect")))reply="Confirmed: cleaning in progress.";
 }
 if(!reply&&now-tg_action_at>=60)reply="No fresh confirmation received within 60 seconds. Check the robot in the web; the command may have worked.";
 if(reply){tg_reply(reply);tg_action_at=0;}
}
static bool tg_parse_command(const char *text,char command[32],char target[64]){
 if(*text!='/')return false;
 const char *end=text;while(*end&&!isspace((unsigned char)*end))end++;
 const char *at=memchr(text,'@',(size_t)(end-text));size_t len=(size_t)((at?at:end)-text);if(len<2||len>=32)return false;
 for(size_t i=0;i<len;i++)command[i]=(char)tolower((unsigned char)text[i]);
 command[len]=0;
 while(isspace((unsigned char)*end))end++;
 size_t size=strlen(end);while(size&&isspace((unsigned char)end[size-1]))size--;if(size>=64)return false;memcpy(target,end,size);target[size]=0;
 return !strcmp(command,"/goto")||!strcmp(command,"/status")||!strcmp(command,"/playsong")||!strcmp(command,"/help")||!strcmp(command,"/start")||!strcmp(command,"/getip")||!strcmp(command,"/getlastreport")||!strcmp(command,"/start2clean")||!strcmp(command,"/dock")||!strcmp(command,"/reboot");
}
static bool tg_authorized_message(cJSON *m){
 cJSON *cid=get(get(m,"chat"),"id");if(!cJSON_IsNumber(cid)||cJSON_IsTrue(get(get(m,"from"),"is_bot"))||get(m,"forward_origin")||get(m,"forward_date"))return false;
 char chat[32];snprintf(chat,sizeof chat,"%.0f",cid->valuedouble);time_t date=(time_t)num(m,"date",0),now=time(NULL);
 return !strcmp(str(get(m,"chat"),"type",""),"private")&&!strcmp(chat,str(tg_config,"chat_id",""))&&date>=tg_control_since&&date<=now+5&&now-date<=120;
}
static void tg_get_ip(void){
 char message[1000]="Local web addresses (same LAN only):";size_t used=strlen(message);struct ifaddrs *interfaces=NULL;
 if(!getifaddrs(&interfaces)){
  for(struct ifaddrs *p=interfaces;p;p=p->ifa_next){if(!p->ifa_addr||p->ifa_addr->sa_family!=AF_INET||!(p->ifa_flags&IFF_UP)||(p->ifa_flags&IFF_LOOPBACK))continue;
   char ip[INET_ADDRSTRLEN];struct sockaddr_in *addr=(struct sockaddr_in*)p->ifa_addr;unsigned first=ntohl(addr->sin_addr.s_addr)>>24;if(first==0||first>=224)continue;if(!inet_ntop(AF_INET,&addr->sin_addr,ip,sizeof ip))continue;
   if(used+100>=sizeof message)break;
   used+=(size_t)snprintf(message+used,sizeof message-used,"\n%s · http://%s:%d/",p->ifa_name,ip,port);
  }freeifaddrs(interfaces);
 }tg_reply(message);
}
static void tg_control_message(cJSON *m){
 if(!tg_control_ready()||!tg_authorized_message(m))return;
 char command[32],target[64];if(!tg_parse_command(str(m,"text",""),command,target))return;
 if(!strcmp(command,"/playsong")){tg_play_song(target);return;}
 if(!strcmp(command,"/goto")){
  char *end;errno=0;long x=strtol(target,&end,10);const char *second=end;
  while(isspace((unsigned char)*second))second++;
  char *tail;long y=strtol(second,&tail,10);
  if(errno||end==target||second==end||tail==second||*tail||x < -20000||x > 20000||y < -20000||y > 20000){tg_reply("Use /goto x y with integer coordinates from the map.");return;}
  if(reset_waiting()){tg_reply("Wait for the map change to finish.");return;}
  if(!strcmp(mode,"demo")){tg_reply("Command simulated.");return;}
  if(!control_bin){tg_reply("Navigation unavailable.");return;}
  char a[24],b[24];snprintf(a,sizeof a,"%ld",x);snprintf(b,sizeof b,"%ld",y);
  char *args[]={(char*)control_bin,"goto",a,b,NULL};
  if(run_process(args,NULL,0))tg_reply("Destination rejected or dispatch failed. Check the robot.");
  else {tg_action_at=0;tg_reply("Destination requested.");event("command","Telegram destination dispatched");save_state();}
  return;
 }

 bool cleaning=!strcmp(command,"/start2clean");
 int power=3;if(cleaning&&*target){if(strlen(target)!=1||target[0]<'1'||target[0]>'3'){tg_reply("Use /start2clean [1-3]: 1 Quiet, 2 Normal, 3 Full (default).");return;}power=target[0]-'0';}
 if(!cleaning&&*target&&strcasecmp(target,str(state,"robot_name",robot))){tg_reply("This bot controls only this robot. Use /help for commands.");return;}
 if(!strcmp(command,"/help")||!strcmp(command,"/start")){tg_enqueue("/help - Show help\n/status - Robot status and system usage\n/playsong [1-5] - Play a song (random if omitted)\n/getip - Open the local web (same LAN)\n/getlastreport - Last cleaning report and map\n/start2clean [1-3] - Clean (1 Quiet, 2 Normal, 3 Full; default 3)\n/goto x y - Go to map coordinates\n/dock - Return to dock\n/reboot - Restart robot",0);return;}
 if(!strcmp(command,"/status")){tg_get_status();return;}
 if(!strcmp(command,"/getip")){tg_get_ip();return;}
 if(!strcmp(command,"/getlastreport")){if(!tg_report_enqueue())tg_reply("No saved cleaning report yet.");return;}
 if(reset_waiting()){tg_reply("Command refused while a map change is pending.");return;}
 if(!strcmp(command,"/reboot")){
  if(strcmp(mode,"demo")&&!control_bin){tg_reply("Reboot control unavailable.");return;}
  /* Journal already consumed this update. Fork a bounded delayed dispatch so
   * the queued acknowledgement gets a chance to leave before shutdown. */
  tg_reply("Reboot requested. Web access will disconnect.");event("command","Telegram reboot dispatch recorded");save_state();
  if(!strcmp(mode,"demo"))return;
  if(tg_reboot_pid){tg_reply("Reboot already pending.");return;}
  pid_t child=fork();if(child>0)tg_reboot_pid=child;if(child==0){if(listener>=0)close(listener);sleep(5);execl(control_bin,control_bin,"reboot",(char*)NULL);_exit(127);}if(child<0)tg_reply("Could not dispatch reboot.");return;
 }
 if(tg_action_at){tg_reply("Previous command confirmation superseded by this command.");tg_action_at=0;}
 time_t requested=time(NULL);char error[200]={0};bool dock=!strcmp(command,"/dock");cJSON *body=cJSON_CreateObject();setnum(body,"fan_power",power);bool ok=issue(dock?"dock":"clean",body,error,sizeof error);cJSON_Delete(body);save_state();
 if(!ok){tg_reply(error);return;}
 if(!strcmp(mode,"demo")){tg_reply("Command simulated.");return;}
 tg_action_at=requested;tg_action_dock=dock;tg_reply(dock?"Return to dock requested.":"Cleaning requested.");
}
static void tg_pair_message(cJSON *m){
 if(tg_expires<=time(NULL))return;
 const char *text=str(m,"text",""),*space=strchr(text,' ');bool cmd=space&&((space-text==5&&!strncmp(text,"/link",5))||(space-text>6&&!strncmp(text,"/link@",6)));
 cJSON *chat=get(m,"chat"),*cid=get(chat,"id");
 if(!cmd||strcmp(space+1,tg_code)||num(m,"date",0)<tg_expires-300||num(m,"date",0)>time(NULL)+5||cJSON_IsTrue(get(get(m,"from"),"is_bot"))||!cJSON_IsNumber(cid))return;
 const char *type=str(chat,"type","");if(strcmp(type,"private")||get(m,"forward_origin")||get(m,"forward_date"))return;
 char chatid[32];snprintf(chatid,sizeof chatid,"%.0f",cid->valuedouble);cJSON *next=cJSON_Duplicate(tg_config,1);setstr(next,"chat_id",chatid);setstr(next,"chat_label",str(chat,"title",str(chat,"first_name","Telegram chat")));
 if(tg_store(next)){cJSON_Delete(tg_config);tg_config=next;tg_expires=0;tg_clear_queue();tg_system_config_changed(true);snprintf(tg_result,sizeof tg_result,"Chat linked");}else cJSON_Delete(next);
}
static bool tg_receive_update(cJSON *update){
 cJSON *n=get(update,"update_id");if(!cJSON_IsNumber(n)||n->valuedouble<0||floor(n->valuedouble)!=n->valuedouble||n->valuedouble>9007199254740991.0)return false;
 long long id=(long long)n->valuedouble;if(id<=tg_seen)return true;
 long long old=tg_seen;time_t old_at=tg_last_update_at;tg_seen=id;tg_last_update_at=time(NULL);if(!tg_journal()){tg_seen=old;tg_last_update_at=old_at;return false;}
 cJSON *m=get(update,"message");tg_pair_message(m);tg_control_message(m);return true;
}
static void tg_control_close(void){
 tg_cancel_poll();tg_control_opened=false;*tg_control_token=0;cJSON_Delete(tg_inbox);tg_inbox=NULL;
}
static bool tg_control_open(void){
 const char *key=str(tg_config,"token","");if(tg_control_opened&&!strcmp(key,tg_control_token))return true;
 tg_control_close();snprintf(tg_control_token,sizeof tg_control_token,"%s",key);
 char file[1024],tag[65];path(file,sizeof file,"telegram-control.json");cJSON *o=tg_read(file,4096);tg_hmac(key,"Zigma Free control identity",tag);
 tg_seen=tg_confirmed=0;tg_last_update_at=time(NULL);
 if(!strcmp(tag,str(o,"token_tag",""))){tg_seen=(long long)tg_number(o,"seen");tg_confirmed=(long long)tg_number(o,"confirmed");tg_last_update_at=(time_t)num(o,"last_update_at",time(NULL));}
 cJSON_Delete(o);tg_journal_failed=false;if(!tg_journal())return false;
 tg_control_since=time(NULL);tg_control_opened=true;return true;
}
static void tg_control_result(cJSON *o){
 cJSON *result=get(o,"result");if(!cJSON_IsArray(result))return;
 cJSON_Delete(tg_inbox);tg_inbox=cJSON_CreateArray();const cJSON *item;int count=0;
 cJSON_ArrayForEach(item,result){
  if(++count>100)break;
  cJSON *id=get((cJSON*)item,"update_id");if(!cJSON_IsNumber(id)||id->valuedouble<0||floor(id->valuedouble)!=id->valuedouble||id->valuedouble>=9007199254740991.0)continue;
  cJSON_AddItemToArray(tg_inbox,cJSON_Duplicate(item,1));
 }
}
static void tg_control_tick(void){
 tg_song_tick();
 if(tg_reboot_pid&&waitpid(tg_reboot_pid,NULL,WNOHANG)==tg_reboot_pid)tg_reboot_pid=0;
 if(!tg_control_wanted()){if(tg_control_opened)tg_control_close();return;}
 if(!tg_control_open()||tg_journal_failed)return;
 if(time(NULL)-tg_last_update_at>6*86400&&!cJSON_GetArraySize(tg_inbox)){tg_seen=tg_confirmed=0;tg_last_update_at=time(NULL);if(!tg_journal())return;}
 while(cJSON_GetArraySize(tg_inbox)){
  cJSON *update=cJSON_DetachItemFromArray(tg_inbox,0);long long id=(long long)tg_number(update,"update_id");
  if(!tg_receive_update(update)){cJSON_Delete(update);return;}
  cJSON_Delete(update);if(id+1>tg_confirmed){tg_confirmed=id+1;if(!tg_journal())return;}
 }
}
static bool tg_control_poll(void){return tg_control_opened&&!tg_journal_failed&&!cJSON_GetArraySize(tg_inbox);}
static void tg_control_view(cJSON *o){
 setstr(o,"receiver",!tg_control_opened?"Off":tg_journal_failed?"Journal unavailable":"This robot");
}
