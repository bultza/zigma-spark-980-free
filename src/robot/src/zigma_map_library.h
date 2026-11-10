#include "zigma_native_identity.h"
/* Private immutable map copies. Only this library is written by the helper. */
#include <sys/file.h>
#include <sys/statvfs.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <time.h>
#define LIB_QUOTA (4u*1024u*1024u)
#define LIB_RESERVE (2u*1024u*1024u)
static cJSON *item(cJSON *o,const char *k){return cJSON_GetObjectItemCaseSensitive(o,k);}
static const char *string(cJSON *o,const char *k){cJSON *v=item(o,k);return cJSON_IsString(v)?v->valuestring:"";}
static void putstr(cJSON *o,const char *k,const char *v){cJSON_DeleteItemFromObjectCaseSensitive(o,k);cJSON_AddStringToObject(o,k,v);}
static void putbool(cJSON *o,const char *k,bool v){cJSON_DeleteItemFromObjectCaseSensitive(o,k);cJSON_AddBoolToObject(o,k,v);}
static bool uid_valid(const char *s){if(strlen(s)!=32)return false;for(int i=0;i<32;i++)if(!strchr("0123456789abcdef",s[i]))return false;return true;}
static bool name_valid(const char *s){if(!*s||strlen(s)>96)return false;bool visible=false;for(const unsigned char *p=(const unsigned char*)s;*p;p++){if(*p<32||*p==127)return false;if(*p!=' ')visible=true;}return visible;}
static void hash_text(const char *s,char out[17]){uint64_t n=UINT64_C(14695981039346656037);for(;*s;s++){n^=(unsigned char)*s;n*=UINT64_C(1099511628211);}snprintf(out,17,"%016"PRIx64,n);}
static bool write_bytes(int fd,const void *bytes,size_t len){size_t off=0;while(off<len){ssize_t n=write(fd,(const char*)bytes+off,len-off);if(n<=0)return false;off+=(size_t)n;}return fsync(fd)==0;}
static int print_json(cJSON *o){if(!o)return 1;char *s=cJSON_PrintUnformatted(o);cJSON_Delete(o);if(!s)return 1;puts(s);free(s);return 0;}
static int lib_error(const char *message){cJSON *o=cJSON_CreateObject();cJSON_AddStringToObject(o,"error",message);return print_json(o);}
static void lib_path(char out[48],const char *uid,const char *suffix){snprintf(out,48,"%s.%s",uid,suffix);}
static cJSON *manifest(int dir,const char *uid){
 char file[48];lib_path(file,uid,"json");int fd=openat(dir,file,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return NULL;
 struct stat st={0};char buf[4097];ssize_t n=-1;if(!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size>0&&st.st_size<=4096)n=read(fd,buf,(size_t)st.st_size);close(fd);if(n!=st.st_size||n<=0)return NULL;buf[n]=0;
 const char *end;cJSON *o=cJSON_ParseWithOpts(buf,&end,1);uint32_t id;
 if(!cJSON_IsObject(o)||strcmp(string(o,"uid"),uid)||!map_id(string(o,"id"),&id)||!name_valid(string(o,"name"))||strlen(string(o,"revision"))!=16||!cJSON_IsBool(item(o,"deleted"))){cJSON_Delete(o);return NULL;}
 char version[17];hash_text(buf,version);putstr(o,"version",version);return o;
}
static bool write_manifest(int dir,const char *uid,cJSON *o){
 cJSON *copy=cJSON_Duplicate(o,1);if(!copy)return false;cJSON_DeleteItemFromObjectCaseSensitive(copy,"version");char *s=cJSON_PrintUnformatted(copy);cJSON_Delete(copy);if(!s)return false;
 char target[48],tmp[80];lib_path(target,uid,"json");snprintf(tmp,sizeof(tmp),".%s.%ld.tmp",uid,(long)getpid());int fd=openat(dir,tmp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);if(fd<0){free(s);return false;}
 bool ok=write_bytes(fd,s,strlen(s));free(s);if(close(fd))ok=false;if(ok&&renameat(dir,tmp,dir,target))ok=false;if(!ok)unlinkat(dir,tmp,0);return ok&&fsync(dir)==0;
}
static bool inventory_usage(int dir,unsigned *count,size_t *bytes){
 *count=0;*bytes=0;DIR *d=fdopendir(openat(dir,".",O_RDONLY|O_DIRECTORY));if(!d)return false;struct dirent *e;bool ok=true;
 while((e=readdir(d))){size_t len=strlen(e->d_name);if(len!=36||strcmp(e->d_name+32,".map"))continue;
  char uid[33];memcpy(uid,e->d_name,32);uid[32]=0;if(!uid_valid(uid))continue;struct stat st;
  if(fstatat(dir,e->d_name,&st,AT_SYMLINK_NOFOLLOW)||!S_ISREG(st.st_mode)||st.st_size<0){ok=false;break;}
  (*count)++;*bytes+=(size_t)st.st_size;
 }closedir(d);return ok;
}
static cJSON *library_list(int dir){
 cJSON *o=cJSON_CreateObject(),*items=cJSON_AddArrayToObject(o,"items");DIR *d=fdopendir(openat(dir,".",O_RDONLY|O_DIRECTORY));if(!d){cJSON_Delete(o);return NULL;}struct dirent *e;unsigned unreadable=0,count=0;
 while((e=readdir(d))){if(strlen(e->d_name)!=37||strcmp(e->d_name+32,".json"))continue;char uid[33];memcpy(uid,e->d_name,32);uid[32]=0;if(!uid_valid(uid))continue;if(++count>MAX_MAPS){closedir(d);cJSON_Delete(o);return NULL;}
  cJSON *m=manifest(dir,uid);if(!m){unreadable++;continue;}uint32_t id;map_id(string(m,"id"),&id);struct map data;char file[48];lib_path(file,uid,"map");
  if(!read_named_map(dir,file,id,&data)){putbool(m,"verified",false);cJSON_AddItemToArray(items,m);unreadable++;continue;}
  if(strcmp(data.revision,string(m,"revision"))){free(data.data);putbool(m,"verified",false);cJSON_AddItemToArray(items,m);unreadable++;continue;}
  free(data.data);putbool(m,"verified",true);cJSON_AddItemToArray(items,m);
 }closedir(d);unsigned files;size_t bytes;if(!inventory_usage(dir,&files,&bytes)){cJSON_Delete(o);return NULL;}
 cJSON_AddNumberToObject(o,"unreadable_count",unreadable);cJSON_AddNumberToObject(o,"used_bytes",bytes);cJSON_AddNumberToObject(o,"quota_bytes",LIB_QUOTA);cJSON_AddNumberToObject(o,"max_copies",MAX_MAPS);cJSON_AddNumberToObject(o,"file_count",files);cJSON_AddNumberToObject(o,"orphan_count",files>count?files-count:0);cJSON_AddBoolToObject(o,"activation_available",true);return o;
}
static cJSON *save_copy(int lib,cJSON *request,const char **error){
 const char *uid=string(request,"uid"),*idtext=string(request,"id"),*rev=string(request,"revision"),*name=string(request,"name"),*source=string(request,"source_dir");uint32_t id;
 if(!uid_valid(uid)||!map_id(idtext,&id)||!name_valid(name)||strlen(rev)!=16||source[0]!='/'){*error="Invalid master-copy request";return NULL;}
 cJSON *old=manifest(lib,uid);if(old){if(!strcmp(string(old,"id"),idtext)&&!strcmp(string(old,"revision"),rev)&&!strcmp(string(old,"name"),name)&&!cJSON_IsTrue(item(old,"deleted"))){char file[48];lib_path(file,uid,"map");struct map existing;if(read_named_map(lib,file,id,&existing)){bool same=!strcmp(existing.revision,rev);free(existing.data);if(same)return old;}}cJSON_Delete(old);*error="Copy identifier already exists; refresh the library";return NULL;}
 int dir=open(source,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(dir<0){*error="Source maps unavailable";return NULL;}struct stat a,b;if(fstat(dir,&a)||fstat(lib,&b)||(a.st_dev==b.st_dev&&a.st_ino==b.st_ino)){close(dir);*error="Library must be separate from robot maps";return NULL;}
 struct map m;bool ok=read_map(dir,id,&m);close(dir);if(!ok){*error="Saved map cannot be read consistently";return NULL;}
 if(strcmp(m.revision,rev)){free(m.data);*error="The robot map changed; refresh before saving";return NULL;}
 uint32_t aux=u32(m.data+m.grid+84);if(aux){size_t cells=(size_t)m.w*m.h;unsigned char *raw=malloc(cells);bool valid=raw&&lz4(m.data+m.grid+GRID_HEADER+m.packed,aux,raw,cells);free(raw);if(!valid){free(m.data);*error="Saved map auxiliary data is invalid";return NULL;}}
 cJSON *decoded=preview(&m);if(!decoded){free(m.data);*error="Saved map failed decoding; no master was saved";return NULL;}cJSON_Delete(decoded);
 char existing_file[48];lib_path(existing_file,uid,"map");struct stat existing_st;bool exists=!fstatat(lib,existing_file,&existing_st,AT_SYMLINK_NOFOLLOW);
 unsigned count;size_t used;struct statvfs space;
 if(!inventory_usage(lib,&count,&used)||(!exists&&count>=MAX_MAPS)||used+(exists?0:m.size)>LIB_QUOTA||fstatvfs(lib,&space)||(uint64_t)space.f_bavail*space.f_frsize<LIB_RESERVE+m.size+8192){free(m.data);*error="Master library is full or robot storage is low. Empty unwanted trash copies first";return NULL;}
 char file[48];lib_path(file,uid,"map");int fd=-1;if(!exists){fd=openat(lib,file,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);if(fd<0){free(m.data);*error="A partial copy already exists; refresh before retrying";return NULL;}
 ok=write_bytes(fd,m.data,m.size);if(close(fd))ok=false;}
 if(!ok){unlinkat(lib,file,0);free(m.data);*error="Copy could not be written";return NULL;}
 /* Reopen and compare the full content revision before publishing its manifest. */
 struct map verify;if(!read_named_map(lib,file,id,&verify)){free(m.data);*error="Saved copy verification failed";return NULL;}
 ok=!strcmp(m.revision,verify.revision);free(verify.data);cJSON *meta=metadata(&m);free(m.data);if(!ok){cJSON_Delete(meta);*error="Saved copy checksum mismatch";return NULL;}
 cJSON_AddStringToObject(meta,"uid",uid);cJSON_AddStringToObject(meta,"name",name);cJSON_AddStringToObject(meta,"kind",!strcmp(string(request,"kind"),"deletion_backup")?"deletion_backup":!strcmp(string(request,"kind"),"reset_backup")?"reset_backup":"master");cJSON_AddNumberToObject(meta,"created_at",time(NULL));cJSON_AddBoolToObject(meta,"deleted",false);
 ok=write_manifest(lib,uid,meta);cJSON_Delete(meta);if(!ok){*error="Copy was written but publication is uncertain; refresh the library";return NULL;}
 return manifest(lib,uid);
}
static int library_command(const char *folder,const char *op,const char *json){
 const char *end;cJSON *r=cJSON_ParseWithOpts(json,&end,1);if(!cJSON_IsObject(r)){cJSON_Delete(r);return lib_error("Invalid library request");}
 int dir=open(folder,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(dir<0){cJSON_Delete(r);return lib_error("Master library unavailable");}
 int lock=openat(dir,".lock",O_RDWR|O_CREAT|O_NOFOLLOW,0600);bool read=!strcmp(op,"list")||!strcmp(op,"preview");if(lock<0||flock(lock,(read?LOCK_SH:LOCK_EX)|LOCK_NB)){if(lock>=0)close(lock);close(dir);cJSON_Delete(r);return lib_error("Master library is busy; refresh before retrying");}
 cJSON *out=NULL;const char *error=NULL;
 if(!strcmp(op,"list")){out=library_list(dir);if(!out)error="Master library could not be read";}
 else if(!strcmp(op,"save")){out=save_copy(dir,r,&error);}
 else{
  const char *uid=string(r,"uid");cJSON *meta=uid_valid(uid)?manifest(dir,uid):NULL;
  if(!meta)error="Master copy not found";
  else if(!strcmp(op,"preview")){
   uint32_t id;map_id(string(meta,"id"),&id);struct map m;char file[48];lib_path(file,uid,"map");
   if(read_named_map(dir,file,id,&m)){if(!strcmp(m.revision,string(meta,"revision")))out=preview(&m);free(m.data);}
   if(out)cJSON_AddStringToObject(out,"uid",uid);else error="Master copy failed verification";
  }else if(strcmp(string(r,"version"),string(meta,"version")))error="Master copy changed; refresh before saving";
  else if(!strcmp(op,"purge")){
   if(!cJSON_IsTrue(item(meta,"deleted"))||!cJSON_IsTrue(item(r,"confirm")))error="Move the copy to Trash and confirm permanent deletion first";
   else{char raw[48],file[48];lib_path(raw,uid,"map");lib_path(file,uid,"json");
    if((unlinkat(dir,raw,0)&&errno!=ENOENT)||unlinkat(dir,file,0)||fsync(dir))error="Deletion was not fully confirmed; refresh the library";
    else{out=cJSON_CreateObject();cJSON_AddBoolToObject(out,"purged",true);}
   }
  }else{
   if(!strcmp(op,"rename")){if(!name_valid(string(r,"name")))error="Choose a valid, shorter name";else putstr(meta,"name",string(r,"name"));}
   else if(!strcmp(op,"trash")){if(!cJSON_IsTrue(item(r,"confirm")))error="Confirm moving this copy to Trash";else putbool(meta,"deleted",true);}
   else if(!strcmp(op,"restore"))putbool(meta,"deleted",false);
   else error="Unsupported library operation";
   if(!error){if(write_manifest(dir,uid,meta))out=manifest(dir,uid);else error="Library change could not be confirmed; refresh";}
  }cJSON_Delete(meta);
 }
 flock(lock,LOCK_UN);close(lock);close(dir);cJSON_Delete(r);return error?lib_error(error):print_json(out);
}
/* Exact Caccamora app-server get_map_id: 0x22714 reads 0x4941c + 0x48.
   The startup script checks the complete vendor SHA; this reader also checks
   the getter machine code before accepting the cached identifier. */
static bool native_identity(const char *pidtext,uint32_t *id){
 uint32_t pid;if(!map_id(pidtext,&pid)||pid<2)return false;char path[80];if(!zigma_native_process((int)pid,"/root/app_server/wifi_app_server_grit"))return false;
 snprintf(path,sizeof(path),"/proc/%u/mem",pid);int fd=open(path,O_RDONLY);if(fd<0)return false;
 const unsigned char expected[]={0x01,0x4b,0x98,0x6c,0x70,0x47,0x00,0xbf,0x1c,0x94,0x04,0x00};unsigned char code[12],a[4],b[4],initialized[4];
 bool ok=pread(fd,initialized,4,0x49d30)==4&&u32(initialized)==1&&pread(fd,code,12,0x22714)==12&&!memcmp(code,expected,12)&&pread(fd,a,4,0x49464)==4&&pread(fd,b,4,0x49464)==4&&!memcmp(a,b,4);close(fd);if(!ok||u32(a)>INT32_MAX)return false;*id=u32(a);return true;
}
static bool native_active(const char *pid,uint32_t *id){return native_identity(pid,id)&&*id>0;}
static bool native_idle(const char *pid){
 char path[80];snprintf(path,sizeof(path),"/proc/%s/mem",pid);int fd=open(path,O_RDONLY);if(fd<0)return false;
 unsigned char a[100],b[100];bool ok=pread(fd,a,sizeof(a),0x48e24)==sizeof(a)&&pread(fd,b,sizeof(b),0x48e24)==sizeof(b)&&!memcmp(a,b,sizeof(a));close(fd);
 uint32_t work=ok?u32(a):0;return ok&&(work==1||work==2||work==4||work==30||work==31)&&u32(a+44)==0;
}
static int native_active_command(const char *pid){uint32_t id;if(!native_active(pid,&id))return lib_error("Native active-map cache unavailable");cJSON *o=cJSON_CreateObject();char text[16];snprintf(text,sizeof(text),"%u",id);cJSON_AddStringToObject(o,"id",text);cJSON_AddStringToObject(o,"source","app_server_current_map_cache");cJSON_AddNumberToObject(o,"cache_read_at",time(NULL));return print_json(o);}
static int native_delete_command(const char *pid,const char *json,bool dry,bool clear){
 const char *end;cJSON *r=cJSON_ParseWithOpts(json,&end,1);uint32_t target,active;bool valid=cJSON_IsObject(r)&&(!item(r,"backup")||cJSON_IsBool(item(r,"backup")))&&map_id(string(r,"id"),&target)&&map_id(string(r,"active_id"),&active)&&(clear?active==target:active!=target)&&cJSON_IsTrue(item(r,"confirm"))&&(!clear||!strcmp(string(r,"confirmation_text"),"CLEAR"));
 if(!valid){cJSON_Delete(r);return lib_error("Invalid map identity or missing explicit confirmation");}
 if(!dry){uint32_t now;if(!native_active(pid,&now)||now!=active){cJSON_Delete(r);return lib_error("Active map changed; no deletion sent");}
  int dir=open(string(r,"source_dir"),O_RDONLY|O_DIRECTORY|O_NOFOLLOW);struct map m;bool same=dir>=0&&read_map(dir,target,&m);if(dir>=0)close(dir);if(same){same=!strcmp(m.revision,string(r,"revision"));free(m.data);}if(!same){cJSON_Delete(r);return lib_error("Map changed; no deletion sent");}
  /* Backup is required unless explicitly disabled by this confirmed request. */
  if(!cJSON_IsFalse(item(r,"backup"))){
  int lib=open(string(r,"library_dir"),O_RDONLY|O_DIRECTORY|O_NOFOLLOW);cJSON *backup=lib>=0&&uid_valid(string(r,"uid"))?manifest(lib,string(r,"uid")):NULL;
  bool backed=backup&&!cJSON_IsTrue(item(backup,"deleted"))&&!strcmp(string(backup,"id"),string(r,"id"))&&!strcmp(string(backup,"revision"),string(r,"revision"));
  if(backed){char file[48];lib_path(file,string(r,"uid"),"map");struct map copy;backed=read_named_map(lib,file,target,&copy);if(backed){backed=!strcmp(copy.revision,string(r,"revision"));free(copy.data);}}
  cJSON_Delete(backup);if(lib>=0)close(lib);if(!backed){cJSON_Delete(r);return lib_error("Verified deletion backup is missing");}
  }
  uint32_t last;if(!native_active(pid,&last)||last!=active||!native_idle(pid)){cJSON_Delete(r);return lib_error("Active map changed; no deletion sent");}
 }
 cJSON *payload=cJSON_CreateObject();cJSON_AddStringToObject(payload,"ProtocolVersion","1.0");cJSON_AddStringToObject(payload,"cmd_origin","app");if(clear)cJSON_AddStringToObject(payload,"WorkStatus","ClearMap");else{cJSON_AddStringToObject(payload,"cmd","del_local_map");cJSON *ids=cJSON_AddArrayToObject(payload,"DelHismapIds");cJSON_AddItemToArray(ids,cJSON_CreateString(string(r,"id")));}cJSON_Delete(r);
 if(dry)return print_json(payload);
 char *wire=cJSON_PrintUnformatted(payload);cJSON_Delete(payload);if(!wire)return 1;
 struct {long type;char text[4096];} message={0,{0}};message.type=clear?1:9;snprintf(message.text,sizeof(message.text),"%s",wire);free(wire);key_t key=ftok("/",1);int q=key==(key_t)-1?-1:msgget(key,0);
 if(q<0||msgsnd(q,&message,sizeof(message.text),IPC_NOWAIT))return lib_error("Deletion not dispatched");
 cJSON *out=cJSON_CreateObject();cJSON_AddBoolToObject(out,"accepted",true);cJSON_AddStringToObject(out,"result","delivered_to_local_queue");return print_json(out);
}
