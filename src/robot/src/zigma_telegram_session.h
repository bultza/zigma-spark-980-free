/* Private checkpoint of observed cleaning progress; never a physical timestamp. */
static char *tg_session_saved;
static cJSON *tg_session_state(void){
 cJSON *o=cJSON_CreateObject();setnum(o,"schema",1);setnum(o,"session",tg_session);setnum(o,"last_finish",tg_last_finish);
 setnum(o,"area",tg_area);setnum(o,"seconds",tg_seconds);setbool(o,"reported",tg_session_reported);
 setbool(o,"statistics",tg_session_stats);setbool(o,"wait_clean_exit",tg_wait_clean_exit);return o;
}
static void tg_session_checkpoint(void){
 if(!*data_dir)return;
 cJSON *o=tg_session_state();char *raw=cJSON_PrintUnformatted(o);cJSON_Delete(o);
 if(raw&&(!tg_session_saved||strcmp(raw,tg_session_saved))){char file[1024];path(file,sizeof file,"telegram-session.json");
  if(atomic_write(file,raw)){free(tg_session_saved);tg_session_saved=raw;return;}}
 free(raw);
}
static void tg_session_restore(void){
 char file[1024];path(file,sizeof file,"telegram-session.json");cJSON *o=tg_read(file,4096);
 const cJSON *area=get(o,"area"),*seconds=get(o,"seconds");time_t session=num(o,"session",0),now=time(NULL);
 if(num(o,"schema",0)==1&&session>=0&&session<=now&&(!session||now-session<=7*86400)&&cJSON_IsNumber(area)&&isfinite(area->valuedouble)&&area->valuedouble>=0&&area->valuedouble<=100000&&cJSON_IsNumber(seconds)&&isfinite(seconds->valuedouble)&&seconds->valuedouble>=0&&seconds->valuedouble<=60000000){
  tg_session=session;tg_last_finish=num(o,"last_finish",0);tg_area=area->valuedouble;tg_seconds=seconds->valuedouble;
  tg_session_reported=cJSON_IsTrue(get(o,"reported"));tg_session_stats=cJSON_IsTrue(get(o,"statistics"));tg_wait_clean_exit=cJSON_IsTrue(get(o,"wait_clean_exit"));
 }
 cJSON_Delete(o);
 /* A report commit may precede its progress checkpoint. Do not generate that
  * same observed session again after a crash between the two commits. */
 path(file,sizeof file,"telegram-report.json");cJSON *report=tg_read(file,4096);
 time_t committed=num(report,"finish_at",0);if(committed>tg_last_finish&&committed<=now)tg_last_finish=committed;
 if(tg_session){
  if(num(report,"session_id",0)==tg_session){tg_session=0;tg_session_reported=true;tg_session_stats=false;tg_area=tg_seconds=0;tg_wait_clean_exit=!cJSON_IsTrue(get(report,"partial_report"));}
 }
 cJSON_Delete(report);
}
