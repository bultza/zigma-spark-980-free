/* Caccamora native import: one trusted raw hoslam_map in a generated tar.gz.
   Never accepts archives, shell fragments or caller-supplied staging paths. */
static bool restore_checksum(const struct map *m){
 if(m->size<156||m->grid<156)return false;
 uint32_t sum=0;for(size_t i=0;i<m->size;i++)if(i<152||i>=156)sum+=m->data[i];
 return sum==u32(m->data+152);
}
static uint32_t gzip_crc(const unsigned char *p,size_t len){uint32_t crc=~0u;for(size_t i=0;i<len;i++){crc^=p[i];for(int j=0;j<8;j++)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1)));}return ~crc;}
static void put32(unsigned char *p,uint32_t v){for(int j=0;j<4;j++)p[j]=(unsigned char)(v>>(8*j));}
static bool restore_package(const struct map *m,const char *dest){
 size_t size=512+((m->size+511)/512)*512+1024;unsigned char *tar=calloc(size,1);if(!tar)return false;
 memcpy(tar,"hoslam_map",10);snprintf((char*)tar+100,8,"%07o",0600);snprintf((char*)tar+108,8,"%07o",0);snprintf((char*)tar+116,8,"%07o",0);
 snprintf((char*)tar+124,12,"%011lo",(unsigned long)m->size);snprintf((char*)tar+136,12,"%011o",0);memset(tar+148,' ',8);tar[156]='0';memcpy(tar+257,"ustar",5);memcpy(tar+263,"00",2);
 unsigned sum=0;for(int i=0;i<512;i++)sum+=tar[i];snprintf((char*)tar+148,7,"%06o",sum);tar[155]=' ';memcpy(tar+512,m->data,m->size);
 size_t cap=18+size+5*((size+65534)/65535),off=10;unsigned char *gz=malloc(cap);if(!gz){free(tar);return false;}
 memcpy(gz,(unsigned char[]){31,139,8,0,0,0,0,0,0,3},10);
 for(size_t n=0;n<size;){size_t part=size-n;if(part>65535)part=65535;gz[off++]=n+part==size?1:0;gz[off++]=part&255;gz[off++]=part>>8;gz[off++]=(~part)&255;gz[off++]=((~part)>>8)&255;memcpy(gz+off,tar+n,part);off+=part;n+=part;}
 put32(gz+off,gzip_crc(tar,size));put32(gz+off+4,(uint32_t)size);off+=8;free(tar);
 int fd=open(dest,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);bool ok=fd>=0&&write_bytes(fd,gz,off);free(gz);if(fd>=0&&close(fd))ok=false;if(!ok)unlink(dest);return ok;
}
static cJSON *restore_context(const char *pid){
 uint32_t id;if(!native_identity(pid,&id))return NULL;
 cJSON *o=cJSON_CreateObject();char text[16];snprintf(text,sizeof(text),"%u",id);cJSON_AddStringToObject(o,"id",text);cJSON_AddBoolToObject(o,"idle",native_idle(pid));
 struct stat log;if(!stat("/tmp/app_server.log",&log)&&S_ISREG(log.st_mode)){cJSON_AddNumberToObject(o,"log_inode",log.st_ino);cJSON_AddNumberToObject(o,"log_offset",log.st_size);}return o;
}
static int restore_probe(const char *pid,const char *json){
 cJSON *r=cJSON_Parse(json),*o=restore_context(pid);if(!o){cJSON_Delete(r);return lib_error("Native restore context unavailable");}
 cJSON *ino=item(r,"log_inode"),*offset=item(r,"log_offset");FILE *f=fopen("/tmp/app_server.log","rb");struct stat st;int result=-999;time_t result_at=0;
 if(f&&cJSON_IsNumber(ino)&&cJSON_IsNumber(offset)&&offset->valuedouble>=0&&!fstat(fileno(f),&st)&&st.st_ino==(ino_t)ino->valuedouble&&st.st_size>=offset->valuedouble&&st.st_size-offset->valuedouble<=262144&&fseeko(f,(off_t)offset->valuedouble,SEEK_SET)==0){
  char line[4096];while(fgets(line,sizeof(line),f)){char *p=strstr(line,"send set map result: ");int value;if(p&&sscanf(p,"send set map result: %d",&value)==1){result=value;struct tm wall={0};int year,month;if(sscanf(line,"[%d-%d-%d %d:%d:%d",&year,&month,&wall.tm_mday,&wall.tm_hour,&wall.tm_min,&wall.tm_sec)==6){wall.tm_year=year-1900;wall.tm_mon=month-1;wall.tm_isdst=-1;result_at=mktime(&wall);}else result_at=0;}}
 }
 if(f)fclose(f);
 cJSON_AddBoolToObject(o,"result_seen",result!=-999);if(result!=-999)cJSON_AddBoolToObject(o,"restored",result==0);if(result_at>0)cJSON_AddNumberToObject(o,"result_at",result_at);cJSON_Delete(r);return print_json(o);
}
static int native_restore_command(const char *pid,const char *json,bool dry){
 cJSON *r=cJSON_Parse(json);const char *error=NULL;uint32_t expected=0,target=0;struct map m={0};int lib=-1,lock=-1;char stage[96]={0},archive[128]={0};bool sent=false,stage_created=false;
 if(!cJSON_IsObject(r)||!uid_valid(string(r,"uid"))||!uid_valid(string(r,"operation_uid"))||!map_id(string(r,"active_id"),&expected)||!cJSON_IsTrue(item(r,"confirm"))||strcmp(string(r,"confirmation_text"),"RESTORE")){error="Invalid restore identity or confirmation";goto done;}
 lib=open(string(r,"library_dir"),O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(lib<0){error="Master library unavailable";goto done;}
 lock=openat(lib,".lock",O_RDWR|O_CREAT|O_NOFOLLOW,0600);if(lock<0||flock(lock,LOCK_EX|LOCK_NB)){error="Master library busy";goto done;}
 cJSON *meta=manifest(lib,string(r,"uid"));char file[48];lib_path(file,string(r,"uid"),"map");
 bool valid=meta&&!cJSON_IsTrue(item(meta,"deleted"))&&!strcmp(string(meta,"version"),string(r,"version"))&&map_id(string(meta,"id"),&target)&&target>0&&read_named_map(lib,file,target,&m)&&!strcmp(m.revision,string(meta,"revision"));cJSON_Delete(meta);
 if(!valid||!restore_checksum(&m)){error="Master changed or failed native integrity verification; refresh copies";goto done;}
 cJSON *decoded=preview(&m);if(!decoded){error="Master cannot be decoded";goto done;}cJSON_Delete(decoded);
 if(!dry){
  uint32_t now;if(!native_identity(pid,&now)||now!=expected||!native_idle(pid)){error="Robot must be idle with unchanged map identity";goto done;}
  struct stat st;const char *reserved[]={"/tmp/tmp.tar","/tmp/tmp.tar.gz","/tmp/hoslam_map"};for(int i=0;i<3;i++)if(lstat(reserved[i],&st)==0||errno!=ENOENT){error="Native import workspace is occupied; no restore sent";goto done;}
  int source=open(string(r,"source_dir"),O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(source<0){error="Robot maps unavailable";goto done;}
  /* Back up both the current map and any saved map sharing the imported ID. */
  uint32_t ids[]={expected,target};for(int i=0;i<2&&!error;i++){
   if((i==0&&!ids[i])||(i==1&&target==expected))continue;
   struct map old;bool exists=read_map(source,ids[i],&old);
   if(!exists){char name[32];snprintf(name,sizeof(name),"MAP_%u",ids[i]);if(ids[i]==expected||fstatat(source,name,&st,AT_SYMLINK_NOFOLLOW)==0||errno!=ENOENT)error="Existing robot map cannot be backed up";continue;}
   cJSON *backup=cJSON_CreateObject();char id[16];snprintf(id,sizeof(id),"%u",ids[i]);cJSON_AddStringToObject(backup,"id",id);cJSON_AddStringToObject(backup,"revision",old.revision);cJSON_AddStringToObject(backup,"uid",string(r,i==0?"active_backup_uid":"target_backup_uid"));cJSON_AddStringToObject(backup,"name","Before master restore");cJSON_AddStringToObject(backup,"source_dir",string(r,"source_dir"));cJSON_AddStringToObject(backup,"kind","reset_backup");free(old.data);
   cJSON *saved=save_copy(lib,backup,&error);cJSON_Delete(saved);cJSON_Delete(backup);
  }close(source);if(error)goto done;
 }
 struct statvfs space;if(statvfs("/tmp",&space)||(uint64_t)space.f_bavail*space.f_frsize<4*m.size+LIB_RESERVE){error="Not enough temporary space to restore safely";goto done;}
 snprintf(stage,sizeof(stage),"/tmp/zigma-restore-%s",string(r,"operation_uid"));if(mkdir(stage,0700)){error="Restore operation already staged; no automatic retry";goto done;}
 stage_created=true;snprintf(archive,sizeof(archive),"%s/map.tar.gz",stage);if(!restore_package(&m,archive)){error="Restore package could not be written";goto done;}
 cJSON *payload=cJSON_CreateObject();cJSON_AddStringToObject(payload,"ProtocolVersion","1.0");cJSON_AddStringToObject(payload,"cmd_origin","app");char id[16];snprintf(id,sizeof(id),"%u",target);cJSON_AddStringToObject(payload,"HisMapId",id);cJSON_AddStringToObject(payload,"HisFilePath",archive);
 if(dry){cJSON_AddBoolToObject(payload,"dry_run",true);free(m.data);flock(lock,LOCK_UN);close(lock);close(lib);cJSON_Delete(r);return print_json(payload);}
 uint32_t last;if(!native_identity(pid,&last)||last!=expected||!native_idle(pid)){cJSON_Delete(payload);error="Robot context changed; no restore sent";goto done;}
 char *wire=cJSON_PrintUnformatted(payload);cJSON_Delete(payload);if(!wire){error="Restore encoding failed";goto done;}
 struct {long type;char text[4096];} message={9,{0}};snprintf(message.text,sizeof(message.text),"%s",wire);free(wire);key_t key=ftok("/",1);int q=key==(key_t)-1?-1:msgget(key,0);
 if(q<0||msgsnd(q,&message,sizeof(message.text),IPC_NOWAIT)){error="Restore was not dispatched; backups kept";goto done;}sent=true;
 done:
 free(m.data);if(lock>=0){flock(lock,LOCK_UN);close(lock);}if(lib>=0)close(lib);cJSON_Delete(r);
 if(!sent){if(*archive)unlink(archive);if(stage_created)rmdir(stage);return lib_error(error?error:"Restore failed");}
 cJSON *out=cJSON_CreateObject();cJSON_AddBoolToObject(out,"accepted",true);cJSON_AddStringToObject(out,"result","delivered_to_local_queue");return print_json(out);
}
