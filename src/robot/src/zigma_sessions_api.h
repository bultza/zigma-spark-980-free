/* Read-only authenticated API; never consumes IPC or sends notifications. */
#include "zigma_sessions.h"
#include "zigma_report_map.h"
static const char *core_records_dir;
static cJSON *sessions_catalog(void){
 static unsigned long long previous;static cJSON *cached;
 if(!core_records_dir)return NULL;
 DIR *d=opendir(core_records_dir);if(!d)return NULL;
 struct dirent *entry;unsigned long long signature=0;bool valid=true;
 while((entry=readdir(d))){
  if(strncmp(entry->d_name,"record-",7)&&strncmp(entry->d_name,"spatial-",8))continue;
  struct stat st;if(fstatat(dirfd(d),entry->d_name,&st,AT_SYMLINK_NOFOLLOW)){valid=false;break;}
  unsigned long long hash=1469598103934665603ULL;
  for(const unsigned char *p=(unsigned char*)entry->d_name;*p;p++)hash=(hash^*p)*1099511628211ULL;
  hash^=(unsigned long long)st.st_ino;hash=hash*1099511628211ULL^(unsigned long long)st.st_size;
  hash=hash*1099511628211ULL^(unsigned long long)st.st_ctim.tv_sec;
  hash=hash*1099511628211ULL^(unsigned long long)st.st_ctim.tv_nsec;
  signature+=hash;
 }
 closedir(d);if(!valid)return NULL;
 if(!cached||signature!=previous){cJSON *next=zs_catalog(core_records_dir);if(!next)return NULL;cJSON_Delete(cached);cached=next;previous=signature;}
 return cJSON_Duplicate(cached,1);
}
/* Local snapshots supplement native history; never masquerade as native records. */
static void sessions_local(cJSON *catalog){
 char file[1024];path(file,sizeof file,"cleaning-reports.json");cJSON *all=tg_read(file,512*1024),*report;
 cJSON_ArrayForEach(report,all){
  time_t end=num(report,"finish_at",0);if(end<=0)end=num(report,"created",0);
  time_t start=num(report,"session_id",0);if(start<=0)start=end;
  if(end<=0||start>end)continue;
  char a[32],b[32];struct tm tm;if(!localtime_r(&start,&tm))continue;strftime(a,sizeof a,"%Y-%m-%d %H:%M",&tm);
  if(!localtime_r(&end,&tm))continue;
  strftime(b,sizeof b,"%Y-%m-%d %H:%M",&tm);
  bool duplicate=false;cJSON *native;cJSON_ArrayForEach(native,get(catalog,"items"))if(!strcmp(str(native,"started",""),a)&&!strcmp(str(native,"ended",""),b)){duplicate=true;break;}
  if(duplicate)continue;
  cJSON *row=cJSON_CreateObject(),*key=cJSON_CreateObject();setnum(key,"local_session",start);char id[65];zs_digest(key,id);cJSON_Delete(key);
  setstr(row,"id",id);setstr(row,"source","local_report");setstr(row,"started",a);setstr(row,"ended",b);setstr(row,"time_basis","robot_local_time");
  setstr(row,"outcome",cJSON_IsTrue(get(report,"partial_report"))?"partial_reported":"local_finish_reported");setstr(row,"summary",str(report,"text",""));setbool(row,"map_available",false);
  const char *fields[]={"clean_area_m2","clean_time_s",NULL};for(int i=0;fields[i];i++)if(cJSON_IsNumber(get(report,fields[i])))setnum(row,fields[i],get(report,fields[i])->valuedouble);
  cJSON_AddItemToArray(get(catalog,"items"),row);
 }
 if(cJSON_GetArraySize(get(catalog,"items")))setbool(catalog,"available",true);
 cJSON_Delete(all);
}
static bool sessions_api(int fd,const char *method,const char *url){
 if(strcmp(method,"GET")||strncmp(url,"/api/sessions",13))return false;
 cJSON *catalog=sessions_catalog();
 if(!catalog){catalog=cJSON_CreateObject();cJSON_AddArrayToObject(catalog,"items");cJSON_AddBoolToObject(catalog,"available",false);}
 else cJSON_AddBoolToObject(catalog,"available",true);
 sessions_local(catalog);
 if(!strcmp(url,"/api/sessions")){json_response(fd,200,catalog);return true;}
 const char *suffix=url+13;char id[65]={0};size_t length=strlen(suffix);
 if(suffix[0]!='/'||(length!=65&&length!=69)){cJSON_Delete(catalog);error_response(fd,404,"Cleaning report not found");return true;}
 memcpy(id,suffix+1,64);bool map=length==69&&!strcmp(suffix+65,"/map");
 if(!zs_hex(id)||(length==69&&!map)){cJSON_Delete(catalog);error_response(fd,404,"Cleaning report not found");return true;}
 cJSON *found=NULL,*candidate;cJSON_ArrayForEach(candidate,get(catalog,"items"))if(!strcmp(str(candidate,"id",""),id)){found=candidate;break;}
 if(!found){cJSON_Delete(catalog);error_response(fd,404,"Cleaning report not found");return true;}
 if(!map){cJSON *copy=cJSON_Duplicate(found,1);cJSON_Delete(catalog);json_response(fd,200,copy);return true;}
 cJSON *event=cJSON_IsTrue(get(found,"map_available"))?zs_event(core_records_dir,"spatial",str(found,"map_event","")):NULL;
 cJSON *decoded=event?zr_map(get(event,"payload")):NULL;cJSON_Delete(event);cJSON_Delete(catalog);
 if(decoded)json_response(fd,200,decoded);else error_response(fd,409,"No unambiguous decodable map for this cleaning report");return true;
}
