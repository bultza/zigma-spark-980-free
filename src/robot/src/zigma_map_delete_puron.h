#include "mbedtls/sha256.h"
/* Puron's JSON deletion handler logs integer count with %s and faults before
 * dispatch. Use its verified navigation message instead: queue ftok("/",14),
 * type 27, uint32 length=8 followed by uint32 target ID, zero-filled remainder.
 * Puron navigation 0x70104 routes type 27 to 0x70fae, which loads the ID
 * at message+8 (after mtype AND length). ID zero means
 * delete-all and must NEVER be sent. No file unlink or vendor patch. */
static bool map_image_hash(const char *path,const char *expected){
 FILE *f=fopen(path,"rb");if(!f)return false;
 mbedtls_sha256_context ctx;mbedtls_sha256_init(&ctx);int rc=mbedtls_sha256_starts(&ctx,0);unsigned char b[8192],digest[32];size_t n;
 while(!rc&&(n=fread(b,1,sizeof b,f)))rc=mbedtls_sha256_update(&ctx,b,n);
 if(ferror(f))rc=1;
 if(!rc)rc=mbedtls_sha256_finish(&ctx,digest);
 fclose(f);mbedtls_sha256_free(&ctx);if(rc)return false;
 char hash[65];for(int i=0;i<32;i++)snprintf(hash+2*i,3,"%02x",digest[i]);return !strcmp(hash,expected);
}
static bool puron_delete_message(uint32_t target,uint32_t active,unsigned char text[4096]){
 if(!target||target>INT32_MAX||active>INT32_MAX||target==active)return false;
 memset(text,0,4096);text[0]=8;
 for(int i=0;i<4;i++)text[4+i]=(unsigned char)(target>>(8*i));
 return u32(text)==8&&u32(text+4)==target&&u32(text+4)!=0;
}
static bool native_is_puron(const char *pid){int fd=native_map_fd(pid);if(fd<0)return false;const struct map_native_profile *p=map_profile_fd(fd);bool yes=p&&!strcmp(p->name,"puron");close(fd);return yes;}
/* Puron ClearMap also omits the length word: app 0x20778 sends only ID,
 * send_msg_to_hoslam_app copies it to payload+0, but navigation reads +4.
 * Use the verified length/ID frame for this separately confirmed operation. */
static bool puron_clear_message(uint32_t target,uint32_t active,unsigned char text[4096]){
 if(!target||target!=active||target>INT32_MAX)return false;
 memset(text,0,4096);text[0]=8;for(int i=0;i<4;i++)text[4+i]=(unsigned char)(target>>(8*i));return true;
}
static int puron_map_dispatch(const char *pid,uint32_t target,uint32_t active,bool clear){
 struct {long type;unsigned char text[4096];} message={27,{0}};
 if(!(clear?puron_clear_message(target,active,message.text):puron_delete_message(target,active,message.text)))return lib_error("Unsafe map deletion identity; nothing sent");
 char path[80];snprintf(path,sizeof path,"/proc/%s/exe",pid);
 if(!map_image_hash(path,"784fbcda505db72675bf01a82f5b02f6eca8cb313a7916176ed7bbf10d7dd912"))return lib_error("Unsupported Puron app-server; nothing sent");
 key_t key=ftok("/",14);int q=key==(key_t)-1?-1:msgget(key,0);struct msqid_ds info;
 if(q<0||msgctl(q,IPC_STAT,&info)||!zigma_native_process(info.msg_lrpid,"/root/hoslam/demo_rplidar"))return lib_error("Puron navigation receiver unavailable; nothing sent");
 snprintf(path,sizeof path,"/proc/%d/exe",info.msg_lrpid);
 if(!map_image_hash(path,"6b7b11091132e75d9787abd59a8ac4f758da3732cccf86dc5702e99bd772a5d8"))return lib_error("Unsupported Puron navigation; nothing sent");
 struct msqid_ds again;uint32_t now;
 if(msgctl(q,IPC_STAT,&again)||!zigma_native_process(again.msg_lrpid,"/root/hoslam/demo_rplidar")||!zigma_same_file(path,"/root/hoslam/demo_rplidar")||!native_identity(pid,&now)||now!=active||!native_idle(pid))return lib_error("Robot or map changed; nothing sent");
 if(msgsnd(q,&message,sizeof message.text,IPC_NOWAIT))return lib_error("Deletion not dispatched");
 cJSON *o=cJSON_CreateObject();cJSON_AddBoolToObject(o,"accepted",true);cJSON_AddStringToObject(o,"result","delivered_to_puron_navigation_queue");return print_json(o);
}

/* Refill the ID cache after an app-server restart with the native current-map
 * request. Puron may save its current snapshot while answering; it does not
 * select or clear a map. Called explicitly, not by the passive reader. */
static int puron_refresh_active(const char *pid){
 if(!native_is_puron(pid))return lib_error("Active-map refresh unavailable for this firmware");
 char path[80];snprintf(path,sizeof path,"/proc/%s/exe",pid);
 if(!map_image_hash(path,"784fbcda505db72675bf01a82f5b02f6eca8cb313a7916176ed7bbf10d7dd912"))return lib_error("Unsupported Puron app-server");
 key_t key=ftok("/",1);int q=key==(key_t)-1?-1:msgget(key,0);struct msqid_ds info;
 if(q<0||msgctl(q,IPC_STAT,&info)||!zigma_native_process(info.msg_lrpid,"/root/app_server/wifi_app_server_grit")||!zigma_same_file(path,"/root/app_server/wifi_app_server_grit"))return lib_error("App-server receiver changed");
 struct {long type;char text[4096];} message={9,"{\"ProtocolVersion\":\"1.0\",\"cmd_origin\":\"app\",\"cmd\":\"req_local_map\",\"MapType\":\"current_reuse_map\"}"};
 if(msgsnd(q,&message,sizeof message.text,IPC_NOWAIT))return lib_error("Active-map identity query not dispatched");
 for(int i=0;i<10;i++){uint32_t id;if(native_active(pid,&id))break;struct timespec delay={0,100000000};nanosleep(&delay,NULL);}
 return native_active_command(pid);
}
