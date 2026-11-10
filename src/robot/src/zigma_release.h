#ifndef ZIGMA_RELEASE_H
#define ZIGMA_RELEASE_H
/* Installed artifact metadata is trusted only when the bytes match the release. */
static const char *release_file="/root/zigma-free/release.json";
static bool release_digest(const char *file,char hex[65]){
 int fd=open(file,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return false;
 struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size>20*1024*1024){close(fd);return false;}
 mbedtls_sha256_context ctx;mbedtls_sha256_init(&ctx);int rc=mbedtls_sha256_starts(&ctx,0);unsigned char buf[16384],hash[32];ssize_t n=0;
 while(!rc&&(n=read(fd,buf,sizeof buf))>0)rc=mbedtls_sha256_update(&ctx,buf,(size_t)n);
 if(n<0)rc=1;
 if(!rc)rc=mbedtls_sha256_finish(&ctx,hash);
 mbedtls_sha256_free(&ctx);close(fd);if(rc)return false;
 for(unsigned i=0;i<32;i++)snprintf(hex+2*i,3,"%02x",hash[i]);
 return true;
}
static cJSON *release_view(void){
 cJSON *out=cJSON_CreateObject(),*items=cJSON_AddArrayToObject(out,"items");
 char *raw=read_file(release_file,65536);cJSON *manifest=raw?strict_json(raw):NULL;free(raw);
 cJSON *list=get(manifest,"items");bool available=cJSON_IsArray(list)&&cJSON_GetArraySize(list)<=64;setbool(out,"available",available);
 if(available){cJSON *entry;cJSON_ArrayForEach(entry,list){
  const char *file=str(entry,"path","");if(!safe_path(file))continue;
  cJSON *item=cJSON_Duplicate(entry,1);char actual[65];bool present=release_digest(file,actual);
  bool matches=present&&!strcmp(actual,str(entry,"sha256",""));
  setstr(item,"status",matches?"verified":present?"modified":"missing");
  if(!matches){cJSON_DeleteItemFromObjectCaseSensitive(item,"version");cJSON_AddNullToObject(item,"version");cJSON_DeleteItemFromObjectCaseSensitive(item,"compiled_at");cJSON_AddNullToObject(item,"compiled_at");cJSON_DeleteItemFromObjectCaseSensitive(item,"prepared_at");}
  cJSON_AddItemToArray(items,item);
 }}
 cJSON_Delete(manifest);return out;
}
#endif
