/* Opt-in durable, content-addressed records. No raw IPC or credentials. */
#include "mbedtls/sha256.h"
#include <sys/statvfs.h>
static const char *data_dir;
static int data_lock=-1;
static bool durable_write(const char *name,const cJSON *o){
 char path[1200],tmp[1220];
 if(snprintf(path,sizeof path,"%s/%s",data_dir,name)>=(int)sizeof path)return false;
 if(snprintf(tmp,sizeof tmp,"%s.new",path)>=(int)sizeof tmp)return false;
 char *raw=cJSON_PrintUnformatted(o);if(!raw)return false;
 int fd=open(tmp,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);bool ok=false;
 if(fd>=0){size_t n=strlen(raw),off=0;while(off<n){ssize_t r=write(fd,raw+off,n-off);if(r<0&&errno==EINTR)continue;if(r<=0)break;off+=(size_t)r;}
  ok=off==n&&!fsync(fd);if(close(fd))ok=false;
 }
 free(raw);
 if(ok&&rename(tmp,path)==0){int dir=open(data_dir,O_RDONLY|O_DIRECTORY);if(dir<0)return false;ok=fsync(dir)==0;close(dir);return ok;}
 unlink(tmp);return false;
}
static bool store_open(void){
 if(!data_dir)return true;
 if(mkdir(data_dir,0700)&&errno!=EEXIST)return false;
 struct stat st;if(lstat(data_dir,&st)||!S_ISDIR(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&0077))return false;
 char path[1200];if(snprintf(path,sizeof path,"%s/store.lock",data_dir)>=(int)sizeof path)return false;
 data_lock=open(path,O_RDWR|O_CREAT|O_NOFOLLOW,0600);
 return data_lock>=0&&flock(data_lock,LOCK_EX|LOCK_NB)==0;
}
static void archive_record(const char *kind,const cJSON *in){
 if(!data_dir)return;
 char *raw=cJSON_PrintUnformatted(in);if(!raw){io_failed=true;return;}
 size_t payload_bytes=strlen(raw);unsigned char digest[32];int result=mbedtls_sha256((unsigned char*)raw,strlen(raw),digest,0);free(raw);
 if(result){io_failed=true;return;}
 char name[100],hex[65];for(unsigned i=0;i<32;i++)snprintf(hex+2*i,3,"%02x",digest[i]);
 snprintf(name,sizeof name,"%s-%s.json",kind,hex);
 char path[1200];if(snprintf(path,sizeof path,"%s/%s",data_dir,name)>=(int)sizeof path){io_failed=true;return;}
 struct stat st;if(!lstat(path,&st)){if(!S_ISREG(st.st_mode))io_failed=true;return;}
 if(errno!=ENOENT){io_failed=true;return;}
 /* Fail visibly rather than silently evict reports when retention is exhausted. */
 DIR *d=opendir(data_dir);if(!d){io_failed=true;return;}unsigned count=0;unsigned long long bytes=0;struct dirent *e;
 while((e=readdir(d)))if(!strncmp(e->d_name,"record-",7)||!strncmp(e->d_name,"spatial-",8)){
  struct stat entry;if(fstatat(dirfd(d),e->d_name,&entry,AT_SYMLINK_NOFOLLOW)||!S_ISREG(entry.st_mode)){closedir(d);io_failed=true;return;}
  count++;bytes+=(unsigned long long)entry.st_size;
 }
 closedir(d);struct statvfs space;
 if(statvfs(data_dir,&space)){io_failed=true;return;}
 unsigned long long available=(unsigned long long)space.f_bavail*space.f_frsize;
 if(count>=128||bytes+payload_bytes+1024>2u*1024u*1024u||available<payload_bytes+1024u*1024u){io_failed=true;last_kind="store_full";return;}
 cJSON *o=stamp();cJSON_AddStringToObject(o,"event_id",hex);cJSON_AddStringToObject(o,"kind",kind);
 cJSON_AddItemToObject(o,"payload",cJSON_Duplicate(in,1));
 if(!durable_write(name,o))io_failed=true;
 cJSON_Delete(o);
}
