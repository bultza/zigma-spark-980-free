#ifndef ZIGMA_PURON_SETTINGS_H
#define ZIGMA_PURON_SETTINGS_H
#include "mbedtls/sha256.h"
/* Puron app-server 784fbcda: recv_packet copies 96 bytes to 0x48980;
 * system_info_init is 0x49870; voice_volume_get reads 0x48ab4 + 2.
 * Cached state is not a fresh sensor measurement. Channel activity is verified separately. */
static bool puron_app_hash(const char *hex){
 return !strcmp(hex,"784fbcda505db72675bf01a82f5b02f6eca8cb313a7916176ed7bbf10d7dd912");
}
static bool puron_settings_process(int pid){
 static int previous_pid;
 static dev_t device;
 static ino_t inode;
 static bool recognized;
 char path[64];struct stat st;
 snprintf(path,sizeof path,"/proc/%d/exe",pid);
 int fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0)return false;
 if(fstat(fd,&st)){close(fd);return false;}
 if(previous_pid==pid&&device==st.st_dev&&inode==st.st_ino){close(fd);return recognized;}
 mbedtls_sha256_context ctx;mbedtls_sha256_init(&ctx);
 int rc=mbedtls_sha256_starts(&ctx,0);unsigned char bytes[16384],digest[32];ssize_t n=0;
 while(!rc&&(n=read(fd,bytes,sizeof bytes))>0)rc=mbedtls_sha256_update(&ctx,bytes,(size_t)n);
 if(n<0)rc=1;
 if(!rc)rc=mbedtls_sha256_finish(&ctx,digest);
 mbedtls_sha256_free(&ctx);close(fd);
 if(rc)return false;
 char hex[65];for(unsigned i=0;i<32;i++)snprintf(hex+2*i,3,"%02x",digest[i]);
 previous_pid=pid;device=st.st_dev;inode=st.st_ino;recognized=puron_app_hash(hex);
 return recognized;
}
static bool cached_puron_settings(cJSON *status,int fd){
 uint32_t initialized=0,again=0;int32_t a[24],b[24];unsigned char volume=255,check=254;
 if(!memory_read(fd,0x49870,&initialized,4)||initialized!=1||
    !memory_read(fd,0x48980,a,sizeof a)||!memory_read(fd,0x48980,b,sizeof b)||memcmp(a,b,sizeof a)||
    !memory_read(fd,0x48ab6,&volume,1)||!memory_read(fd,0x48ab6,&check,1)||volume!=check||volume>100||
    !memory_read(fd,0x49870,&again,4)||again!=1||
    (a[4]!=0&&a[4]!=1)||(a[7]!=0&&a[7]!=1))return false;
 replace_value(status,"continue_clean",cJSON_CreateBool(a[4]));
 replace_value(status,"over_ridge",cJSON_CreateBool(a[7]));
 replace_value(status,"volume",cJSON_CreateNumber(volume));
 replace_value(status,"robot_settings_source",cJSON_CreateString("puron_app_server_cache"));
 replace_value(status,"robot_settings_read_at",cJSON_CreateNumber(time(NULL)));
 return true;
}
/* Raw status fields share the recv_packet layout of the original navigation.
 * Puron app-server exports words9/10 as CleanArea/CleanTime; its ygclient
 * record handler retains m2 and converts minutes to seconds. */
static bool cached_puron_status(cJSON *status,int fd){
 uint32_t initialized=0,again=0;int32_t a[24],b[24];
 if(!memory_read(fd,0x49870,&initialized,4)||initialized!=1||
    !memory_read(fd,0x48980,a,sizeof a)||!memory_read(fd,0x48980,b,sizeof b)||memcmp(a,b,sizeof a)||
    !memory_read(fd,0x49870,&again,4)||again!=1)return false;
 int code=a[0],battery=a[3],error=a[11];
 if(!strcmp(vendor_state(code),"unknown")||battery<0||battery>100||error<0||error>255||
    a[9]<0||a[9]>100000||a[10]<0||a[10]>10080)return false;
 bool matching=number(status,"vendor_state_code")==code&&number(status,"battery_level")==battery&&
  number(status,"vendor_error_code")==error&&number(status,"vendor_clean_area_raw")==a[9]&&number(status,"vendor_clean_time_raw")==a[10];
 if(!matching)replace_value(status,"observed_at",cJSON_CreateNumber(0));
 replace_value(status,"working_status",cJSON_CreateString(vendor_state(code)));
 char message[40];snprintf(message,sizeof message,"VendorError%d",error);
 replace_value(status,"error",cJSON_CreateString(error?message:"NoError"));
 replace_value(status,"vendor_state_code",cJSON_CreateNumber(code));
 replace_value(status,"battery_level",cJSON_CreateNumber(battery));
 replace_value(status,"vendor_error_code",cJSON_CreateNumber(error));
 replace_value(status,"vendor_clean_area_raw",cJSON_CreateNumber(a[9]));
 replace_value(status,"vendor_clean_time_raw",cJSON_CreateNumber(a[10]));
 replace_value(status,"clean_area_m2",cJSON_CreateNumber(a[9]));
 replace_value(status,"clean_time_s",cJSON_CreateNumber((double)a[10]*60));
 replace_value(status,"statistics_units_source",cJSON_CreateString("puron_vendor_history_contract"));
 replace_value(status,"telemetry_source",cJSON_CreateString("app_server_cache"));
 replace_value(status,"app_server_profile",cJSON_CreateString("puron"));
 replace_value(status,"cache_read_at",cJSON_CreateNumber(time(NULL)));
 return true;
}
#endif
