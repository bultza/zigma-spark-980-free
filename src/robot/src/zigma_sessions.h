#ifndef ZIGMA_SESSIONS_H
#define ZIGMA_SESSIONS_H
/* Rebuildable projection over immutable private events. No IPC or cloud access. */
#include <dirent.h>
#include "mbedtls/sha256.h"
static const cJSON *zs_get(const cJSON *o,const char *k){return cJSON_GetObjectItemCaseSensitive(o,k);}
static bool zs_hex(const char *s){if(!s||strlen(s)!=64)return false;for(unsigned i=0;i<64;i++)if(!strchr("0123456789abcdef",s[i]))return false;return true;}
static bool zs_digest(const cJSON *o,char hex[65]){
 char *raw=cJSON_PrintUnformatted(o);if(!raw)return false;unsigned char hash[32];int r=mbedtls_sha256((unsigned char*)raw,strlen(raw),hash,0);free(raw);
 if(r)return false;
 for(unsigned i=0;i<32;i++)snprintf(hex+2*i,3,"%02x",hash[i]);
 return true;
}
static cJSON *zs_load(const char *dir,const char *name,size_t max){
 char path[1200];if(snprintf(path,sizeof path,"%s/%s",dir,name)>=(int)sizeof path)return NULL;
 int fd=open(path,O_RDONLY|O_NOFOLLOW);if(fd<0)return NULL;struct stat st;
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||(unsigned long long)st.st_size>max){close(fd);return NULL;}
 size_t size=(size_t)st.st_size;char *raw=malloc(size+1);if(!raw){close(fd);return NULL;}size_t off=0;
 while(off<size){ssize_t n=read(fd,raw+off,size-off);if(n<0&&errno==EINTR)continue;if(n<=0)break;off+=(size_t)n;}
 close(fd);raw[off]=0;cJSON *o=off==size?cJSON_ParseWithLengthOpts(raw,size+1,NULL,1):NULL;free(raw);
 if(!cJSON_IsObject(o)){cJSON_Delete(o);return NULL;}return o;
}
static cJSON *zs_event(const char *dir,const char *kind,const char *id){
 if(!zs_hex(id))return NULL;
 char name[100];snprintf(name,sizeof name,"%s-%s.json",kind,id);
 cJSON *e=zs_load(dir,name,262144);const cJSON *payload=zs_get(e,"payload");char hash[65];
 const cJSON *tag=zs_get(e,"event_id"),*k=zs_get(e,"kind");
 if(!cJSON_IsObject(payload)||!cJSON_IsString(tag)||strcmp(tag->valuestring,id)||!cJSON_IsString(k)||strcmp(k->valuestring,kind)||!zs_digest(payload,hash)||strcmp(hash,id)){cJSON_Delete(e);return NULL;}
 return e;
}
static bool zs_time(const cJSON *v){
 /* Vendor mHandleUploadCleanRecord parses YYYY-MM-DD HH:MM; keep wall time raw. */
 if(cJSON_IsNumber(v))return isfinite(v->valuedouble)&&v->valuedouble>=0&&v->valuedouble<=4102444800.0&&floor(v->valuedouble)==v->valuedouble;
 if(!cJSON_IsString(v)||strlen(v->valuestring)!=16)return false;
 const char *s=v->valuestring;for(unsigned i=0;i<16;i++){if(i==4||i==7){if(s[i]!='-')return false;}else if(i==10){if(s[i]!=' ')return false;}else if(i==13){if(s[i]!=':')return false;}else if(s[i]<'0'||s[i]>'9')return false;}
 int y,m,d,h,n;if(sscanf(s,"%d-%d-%d %d:%d",&y,&m,&d,&h,&n)!=5||y<2000||y>2100||m<1||m>12||h>23||n>59)return false;
 static const int days[]={31,28,31,30,31,30,31,31,30,31,30,31};int limit=days[m-1]+(m==2&&(y%4==0&&(y%100!=0||y%400==0)));
 return d>=1&&d<=limit;
}
static bool zs_interval(const cJSON *p,char id[65]){
 const cJSON *start=zs_get(p,"CStartTime"),*end=zs_get(p,"CEndTime");if(!zs_time(start)||!zs_time(end)||start->type!=end->type)return false;
 if((cJSON_IsString(start)&&strcmp(start->valuestring,end->valuestring)>0)||(cJSON_IsNumber(start)&&start->valuedouble>end->valuedouble))return false;
 cJSON *key=cJSON_CreateObject();cJSON_AddItemToObject(key,"start",cJSON_Duplicate(start,1));cJSON_AddItemToObject(key,"end",cJSON_Duplicate(end,1));bool ok=zs_digest(key,id);cJSON_Delete(key);return ok;
}
static const cJSON *zs_map_id(const cJSON *p){const cJSON *id=zs_get(p,"map_id");if(!id)id=zs_get(p,"HisMapId");return cJSON_IsString(id)&&strlen(id->valuestring)<=64?id:NULL;}
static void zs_replace(cJSON *o,const char *key,cJSON *v){cJSON_DeleteItemFromObjectCaseSensitive(o,key);if(v)cJSON_AddItemToObject(o,key,v);}
static cJSON *zs_catalog(const char *dir){
 cJSON *out=cJSON_CreateObject(),*items=cJSON_AddArrayToObject(out,"items");cJSON_AddNumberToObject(out,"schema",1);
 unsigned orphan=0,bad=0,total=0;DIR *d=opendir(dir);if(!d){cJSON_Delete(out);return NULL;}
 /* Pass 0 establishes records; pass 1 joins historical maps by exact interval. */
 for(unsigned pass=0;pass<2;pass++){
  rewinddir(d);struct dirent *entry;const char *kind=pass?"spatial":"record";size_t prefix=strlen(kind)+1;
  while((entry=readdir(d))){
   if(strncmp(entry->d_name,kind,prefix-1)||entry->d_name[prefix-1]!='-'||strlen(entry->d_name)!=prefix+69||strcmp(entry->d_name+prefix+64,".json"))continue;
   if(++total>128){bad++;break;}char event_id[65];memcpy(event_id,entry->d_name+prefix,64);event_id[64]=0;
   cJSON *event=zs_event(dir,kind,event_id);const cJSON *p=zs_get(event,"payload");char id[65];
   if(!event){bad++;continue;}if(!zs_interval(p,id)){orphan++;cJSON_Delete(event);continue;}
   cJSON *session=NULL,*candidate; cJSON_ArrayForEach(candidate,items){const cJSON *v=zs_get(candidate,"id");if(cJSON_IsString(v)&&!strcmp(v->valuestring,id)){session=candidate;break;}}
   if(!session&&pass){orphan++;cJSON_Delete(event);continue;}
   if(!session){session=cJSON_CreateObject();cJSON_AddStringToObject(session,"id",id);cJSON_AddItemToArray(items,session);
    cJSON_AddItemToObject(session,"started",cJSON_Duplicate(zs_get(p,"CStartTime"),1));cJSON_AddItemToObject(session,"ended",cJSON_Duplicate(zs_get(p,"CEndTime"),1));
    cJSON_AddStringToObject(session,"time_basis",cJSON_IsString(zs_get(p,"CStartTime"))?"native_local_time_timezone_unknown":"native_epoch");
    cJSON_AddStringToObject(session,"outcome","recorded_completion_unknown");cJSON_AddArrayToObject(session,"record_events");cJSON_AddArrayToObject(session,"map_events");
   }
   if(!pass){
    cJSON_AddItemToArray((cJSON*)zs_get(session,"record_events"),cJSON_CreateString(event_id));
    const char *fields[]={"CleanArea","CleanTime",NULL},*dest[]={"clean_area_m2","clean_time_s"};
    for(unsigned i=0;fields[i];i++){const cJSON *v=zs_get(p,fields[i]);if(cJSON_IsNumber(v)&&isfinite(v->valuedouble)&&v->valuedouble>=0&&v->valuedouble<=1000000){
      double value=v->valuedouble*(i?60:1);const cJSON *prior=zs_get(session,dest[i]);
      if(prior&&prior->valuedouble!=value)zs_replace(session,"statistics_conflict",cJSON_CreateBool(true));
      else if(!prior)cJSON_AddNumberToObject(session,dest[i],value);
    }}
    const cJSON *finish=zs_get(p,"CleanFinish");if(cJSON_IsBool(finish)){
     const char *value=cJSON_IsTrue(finish)?"finished_reported":"partial_reported";const cJSON *prior=zs_get(session,"outcome");
     if(strcmp(prior->valuestring,"recorded_completion_unknown")&&strcmp(prior->valuestring,value))zs_replace(session,"completion_conflict",cJSON_CreateBool(true));
     else zs_replace(session,"outcome",cJSON_CreateString(value));
    }
    const cJSON *map=zs_map_id(p),*prior=zs_get(session,"map_id");if(map){if(prior&&!cJSON_Compare(prior,map,1))zs_replace(session,"map_identity_conflict",cJSON_CreateBool(true));else if(!prior)cJSON_AddItemToObject(session,"map_id",cJSON_Duplicate(map,1));}
   }else if(cJSON_IsString(zs_get(p,"MapData"))){
    const cJSON *expected=zs_get(session,"map_id"),*actual=zs_map_id(p);
    if(!expected||!actual||!cJSON_Compare(expected,actual,1)||cJSON_IsTrue(zs_get(session,"map_identity_conflict")))orphan++;
    else cJSON_AddItemToArray((cJSON*)zs_get(session,"map_events"),cJSON_CreateString(event_id));
   }
   cJSON_Delete(event);
  }
 }
 closedir(d);cJSON *s;cJSON_ArrayForEach(s,items){
  if(cJSON_IsTrue(zs_get(s,"statistics_conflict"))){cJSON_DeleteItemFromObjectCaseSensitive(s,"clean_area_m2");cJSON_DeleteItemFromObjectCaseSensitive(s,"clean_time_s");}
  if(cJSON_IsTrue(zs_get(s,"completion_conflict")))zs_replace(s,"outcome",cJSON_CreateString("conflicting_reports"));
  bool map=cJSON_GetArraySize(zs_get(s,"map_events"))==1&&!cJSON_IsTrue(zs_get(s,"map_identity_conflict"));cJSON_AddBoolToObject(s,"map_available",map);
  if(map)cJSON_AddItemToObject(s,"map_event",cJSON_Duplicate(cJSON_GetArrayItem(zs_get(s,"map_events"),0),1));
  if(cJSON_IsTrue(zs_get(s,"map_identity_conflict")))cJSON_DeleteItemFromObjectCaseSensitive(s,"map_id");
 }
 cJSON_AddNumberToObject(out,"unassociated_records",orphan);cJSON_AddNumberToObject(out,"integrity_errors",bad);return out;
}
#endif
