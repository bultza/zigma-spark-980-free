/* Protocol has no request ID: report state evidence without claiming causality. */
static double cf_clock(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static char *cf_boot(void){char *s=read_file("/proc/sys/kernel/random/boot_id",64);if(s)s[strcspn(s,"\r\n")]=0;return s;}
static void cf_begin(const char *action,cJSON *baseline){
 cJSON *o=cJSON_CreateObject();setstr(o,"action",action);setstr(o,"phase","pending");setstr(o,"baseline",str(baseline,"working_status","unknown"));
 setstr(o,"baseline_error",str(baseline,"error","unknown"));setnum(o,"dispatched_at",time(NULL));setnum(o,"dispatched_monotonic",cf_clock());
 char *boot=cf_boot();setstr(o,"boot_id",boot?boot:"");free(boot);
 cJSON_DeleteItemFromObjectCaseSensitive(state,"command_feedback");cJSON_AddItemToObject(state,"command_feedback",o);
}
static void cf_view(cJSON *status){
 if(!cJSON_IsObject(status))return;
 cJSON *o=get(state,"command_feedback");if(!cJSON_IsObject(o))return;
 if(!strcmp(str(o,"phase",""),"pending")){
  const char *phase=NULL,*work=str(status,"working_status","unknown"),*action=str(o,"action","");
  const cJSON *mono=get(o,"dispatched_monotonic");double sent=cJSON_IsNumber(mono)?mono->valuedouble:0,now=cf_clock();char *boot=cf_boot();
  if(!boot||strcmp(boot,str(o,"boot_id",""))||now<sent)phase="interrupted";
  else if(now-sent>=60)phase="unconfirmed_timeout";
  else{
   const cJSON *arrival=get(status,"core_report_received_monotonic_ms");
   bool after=cJSON_IsTrue(get(status,"core_active"))&&cJSON_IsTrue(get(status,"core_report_matches_cache"))&&cJSON_IsNumber(arrival)&&arrival->valuedouble/1000>sent&&arrival->valuedouble/1000<=now;
   if(!after)after=cJSON_IsTrue(get(status,"fresh"))&&num(status,"observed_at",0)>num(o,"dispatched_at",0);
   if(after){
    const char *error=str(status,"error","unknown");
    if(strcmp(error,"NoError")&&strcmp(error,"unknown")&&strcmp(error,str(o,"baseline_error","unknown")))phase="error_reported";
    bool expected=(!strcmp(action,"pause")&&(!strcmp(work,"Pause")||!strcmp(work,"Standby")))||(!strcmp(action,"dock")&&(!strcmp(work,"BackCharging")||!strcmp(work,"PileCharging")||!strcmp(work,"DirCharging")))||((!strcmp(action,"clean")||!strcmp(action,"resume"))&&(!strcmp(work,"AutoClean")||!strcmp(work,"SpotClean")||!strcmp(work,"SelectClean")||!strcmp(work,"PlanningRect")));
    if(!phase&&expected&&strcmp(work,str(o,"baseline","unknown")))phase="expected_state_reported";
   }
  }
  free(boot);if(phase){setstr(o,"phase",phase);setnum(o,"resolved_at",time(NULL));event("command",!strcmp(phase,"expected_state_reported")?"Robot reported the requested state after dispatch":!strcmp(phase,"error_reported")?"Robot reported an error after dispatch":"Command confirmation unavailable; no automatic retry");save_state();}
 }
 cJSON_AddItemToObject(status,"command_feedback",cJSON_Duplicate(o,1));
}
