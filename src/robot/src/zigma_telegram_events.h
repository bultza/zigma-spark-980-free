/* Additional notifications use observed transitions, never command dispatch. */
enum tg_motion { TG_UNKNOWN, TG_CLEAN, TG_RETURN, TG_CHARGE, TG_IDLE, TG_OTHER };
static enum tg_motion tg_motion_value=TG_UNKNOWN,tg_candidate=TG_UNKNOWN;
static time_t tg_candidate_at,tg_low_at,tg_full_at;
static bool tg_low_latched,tg_full_latched,tg_events_initialized,tg_error_announced;
static bool tg_docked,tg_dock_candidate,tg_dock_initialized;
static time_t tg_dock_at;
static enum tg_motion tg_motion_of(const char *work){
 if(!strcmp(work,"AutoClean")||!strcmp(work,"SpotClean")||!strcmp(work,"SelectClean")||!strcmp(work,"PlanningRect"))return TG_CLEAN;
 if(!strcmp(work,"BackCharging"))return TG_RETURN;
 if(!strcmp(work,"PileCharging")||!strcmp(work,"DirCharging"))return TG_CHARGE;
 if(!strcmp(work,"Standby")||!strcmp(work,"Hibernating"))return TG_IDLE;
 if(!strcmp(work,"Malfunction")||!strcmp(work,"LocationAlarm")||!strcmp(work,"PlanningLocation")||!strcmp(work,"DirectionControl")||!strcmp(work,"CleanDone"))return TG_OTHER;
 return TG_UNKNOWN;
}
static void tg_events_reset(void){
 tg_motion_value=tg_candidate=TG_UNKNOWN;tg_candidate_at=tg_low_at=tg_full_at=0;
 tg_low_latched=tg_full_latched=tg_events_initialized=tg_error_announced=false;
 tg_docked=tg_dock_candidate=tg_dock_initialized=false;tg_dock_at=0;
}
static bool tg_notice(const char *text){return tg_enqueue(text,0);}
static void tg_events_observe(cJSON *s,time_t now,bool baseline){
 const char *work=str(s,"working_status","");enum tg_motion motion=tg_motion_of(work);int battery=num(s,"battery_level",-1);bool valid_battery=battery>=0&&battery<=100;
 if(baseline||!tg_events_initialized){
  tg_dock_initialized=motion!=TG_UNKNOWN;tg_docked=tg_dock_candidate=!strcmp(work,"PileCharging");tg_dock_at=now;
  tg_events_initialized=true;tg_motion_value=tg_candidate=motion;tg_candidate_at=now;
  tg_low_latched=valid_battery&&battery<=20&&motion!=TG_CHARGE;
  tg_full_latched=valid_battery&&battery>=100;tg_low_at=tg_full_at=0;return;
 }
 if(motion==TG_UNKNOWN){tg_dock_at=0;tg_candidate=TG_UNKNOWN;tg_candidate_at=now;tg_low_at=tg_full_at=0;return;}
 /* Dock arrival is independent of activity/cleaning reports. Direct charging
  * does not prove dock arrival. Debounce departure too, to reject contact flicker. */
 bool dock=!strcmp(work,"PileCharging");
 if(!tg_dock_initialized){tg_dock_initialized=true;tg_docked=tg_dock_candidate=dock;tg_dock_at=now;}
 if(dock!=tg_dock_candidate||!tg_dock_at){tg_dock_candidate=dock;tg_dock_at=now;}
 if(dock!=tg_docked&&now-tg_dock_at>=5){
  tg_docked=dock;
  if(dock&&cJSON_IsTrue(get(tg_config,"docking")))tg_notice("Docking completed · Charging at the dock");
 }
 if(motion!=tg_candidate){tg_candidate=motion;tg_candidate_at=now;}
 if(motion!=tg_motion_value&&now-tg_candidate_at>=(motion==TG_IDLE?15:5)){
  enum tg_motion previous=tg_motion_value;tg_motion_value=motion;
  if(cJSON_IsTrue(get(tg_config,"activity"))){
   if(motion==TG_CLEAN)tg_notice("Cleaning in progress");
   else if(motion==TG_RETURN)tg_notice("Returning to the dock");
   else if(motion==TG_CHARGE&&(!cJSON_IsTrue(get(tg_config,"docking"))||strcmp(work,"PileCharging")))tg_notice(!strcmp(work,"PileCharging")?"At the dock · charging":"Charging");
   else if(motion==TG_IDLE&&(previous==TG_CLEAN||previous==TG_RETURN)&&num(s,"vendor_error_code",0)==0&&now-tg_last_finish>20)tg_notice("Cleaning stopped");
  }
 }
 if(!valid_battery){tg_low_at=tg_full_at=0;return;}
 /* Hysteresis prevents 20/21% and 99/100% oscillations from repeating alerts. */
 if(battery>=25)tg_low_latched=false;
 if(battery<=95)tg_full_latched=false;
 if(battery<=20&&motion!=TG_CHARGE&&!tg_low_latched){
  if(!tg_low_at)tg_low_at=now;
  if(now-tg_low_at>=5){tg_low_latched=true;tg_low_at=0;if(cJSON_IsTrue(get(tg_config,"battery"))){char text[80];snprintf(text,sizeof text,"Low battery · %d%%",battery);tg_notice(text);}}
 }else tg_low_at=0;
 if(battery==100&&motion==TG_CHARGE&&!tg_full_latched){
  if(!tg_full_at)tg_full_at=now;
  if(now-tg_full_at>=5){tg_full_latched=true;tg_full_at=0;if(cJSON_IsTrue(get(tg_config,"battery")))tg_notice("Battery fully charged");}
 }else tg_full_at=0;
}
