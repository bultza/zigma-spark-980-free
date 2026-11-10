/* Keep only the latest generated report. Alternate image slots so a failed
 * metadata commit never pairs an old caption with a newer cleaning map. */
static bool tg_report_store(cJSON *job){
 if(!*data_dir)return false;
 char meta[1024],photo[1024];path(meta,sizeof meta,"telegram-report.json");
 cJSON *old=tg_read(meta,4096);int slot=num(old,"slot",0)==0?1:0;cJSON_Delete(old);
 char basename[40];snprintf(basename,sizeof basename,"telegram-report-%d.png",slot);path(photo,sizeof photo,basename);
 cJSON *report=cJSON_CreateObject();setstr(report,"text",str(job,"text",""));setnum(report,"created",num(job,"created",time(NULL)));setnum(report,"slot",slot);setnum(report,"session_id",num(job,"session_id",0));setbool(report,"partial_report",cJSON_IsTrue(get(job,"partial_report")));
 setnum(report,"finish_at",num(job,"finish_at",0));
 size_t size=0;unsigned char *bytes=binary_read(str(job,"photo",""),4*1024*1024,&size);
 bool image=bytes&&binary_write(photo,bytes,size,false);free(bytes);setbool(report,"image",image);
 char *json=cJSON_PrintUnformatted(report);bool ok=json&&atomic_write(meta,json);free(json);cJSON_Delete(report);
 if(ok){snprintf(basename,sizeof basename,"telegram-report-%d.png",1-slot);path(photo,sizeof photo,basename);unlink(photo);}
 return ok;
}
static bool tg_report_enqueue(void){
 char meta[1024];path(meta,sizeof meta,"telegram-report.json");cJSON *report=tg_read(meta,4096);
 if(!report||!*str(report,"text","")){cJSON_Delete(report);return false;}
 char dated[1200],stamp[80]="Unknown";time_t created=(time_t)num(report,"created",0);struct tm local;
 if(created>0&&localtime_r(&created,&local))strftime(stamp,sizeof stamp,"%Y-%m-%d %H:%M:%S %Z",&local);
 const char *body=str(report,"text","");
 /* Older reports contain the historical robot name; strip only recognized
  * report-title prefixes, even if the user has since renamed the robot. */
 const char *separator=strstr(body," · ");
 if(separator&&(!strncmp(separator+4,"Cleaning finished",17)||!strncmp(separator+4,"Partial cleaning report",23)))body=separator+4;
 char compact[1000];snprintf(compact,sizeof compact,"%s",body);
 const char *footer="\nRecorded map and route";size_t len=strlen(compact),tail=strlen(footer);
 if(len>=tail&&!strcmp(compact+len-tail,footer))compact[len-tail]=0;
 snprintf(dated,sizeof dated,"Report date: %s\n%.950s",stamp,compact);
 if(!tg_enqueue(dated,0)){cJSON_Delete(report);return false;}
 cJSON *job=cJSON_GetArrayItem(tg_queue,cJSON_GetArraySize(tg_queue)-1);
 if(cJSON_IsTrue(get(report,"image"))){
  char filename[40],source[1024],dest[512],id[33];snprintf(filename,sizeof filename,"telegram-report-%d.png",num(report,"slot",0)==1?1:0);path(source,sizeof source,filename);
  size_t size;unsigned char *bytes=binary_read(source,4*1024*1024,&size);
  if(bytes&&tg_temp()&&tg_random(id)){snprintf(filename,sizeof filename,"%s.png",id);tg_file(dest,sizeof dest,filename);if(binary_write(dest,bytes,size,false))setstr(job,"photo",dest);}
  free(bytes);
 }
 cJSON_Delete(report);return true;
}

/* A previous partial report suppresses only a completion whose inferred
 * cleaning interval overlaps that reported session, not every future finish. */
static bool tg_finish_already_reported(cJSON *status,time_t finish){
 char file[1024];path(file,sizeof file,"telegram-report.json");cJSON *r=tg_read(file,4096);
 bool same=num(r,"finish_at",0)==finish;
 if(!same&&tg_session_reported&&cJSON_IsTrue(get(r,"partial_report"))){
  time_t start=num(r,"session_id",0),created=num(r,"created",0);double seconds=num(status,"clean_time_s",0);
  if(start>0&&created>0&&finish>=start){
   if(seconds>0&&seconds<=7*86400){double inferred=finish-seconds;same=inferred>=start-120&&inferred<=created;}
   else same=finish>=created&&finish-created<=300;
  }
 }
 cJSON_Delete(r);return same;
}
