#define _POSIX_C_SOURCE 200809L
#include "zigma_version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include "zigma_native_identity.h"
#include "mbedtls/sha256.h"
#include <sys/ipc.h>
#include <sys/msg.h>
#include "cJSON.h"
/* Exact running-image profiles. Queue receivers may be thread IDs. */
static int zones_profile_hash(const char *hash){
 if(!strcmp(hash,"8e323cfe110e4aa78d680b34c6c26a4276ada7869d58f49de64c0440fa551f6c"))return 3;
 if(!strcmp(hash,"784fbcda505db72675bf01a82f5b02f6eca8cb313a7916176ed7bbf10d7dd912"))return 2;
 if(!strcmp(hash,"4cf0709919f0e894a5d3ed1c60ca9d6bf11d7c47120077e23f6c13902c5d9a9a")||!strcmp(hash,"8084000b686d071f8f0590448087ff88d71deee3e55e772c86d41f27d704516c"))return 1;
 return 0;
}
/* Exact reviewed images. Issue04 type16 dispatcher 0x76faa -> 0x77e20
 * replies with type17 at 0x77e60; physical validation remains pending. */
static bool zones_navigation_hash(const char *hash){
 return !strcmp(hash,"b10fc7b48d1bdb990a2c37b82f7d45fa4fd7358f428f42d58ae885aca2970255")||
        !strcmp(hash,"6b7b11091132e75d9787abd59a8ac4f758da3732cccf86dc5702e99bd772a5d8")||
        !strcmp(hash,"1b2751efd2a4d5afc208fce099401d90c581f0cec41cfe440e9a39d519ece664");
}
static bool zones_refresh_pair(const char *app,const char *nav){
 int profile=zones_profile_hash(app);
 return profile==3?!strcmp(nav,"f793e93abb85a1a7b1cbe8cec1f53b2c6d9aac7c21c8fb3b956f460fe693f88c"):(profile&&zones_navigation_hash(nav));
}
#include "zigma_queue_identity.h"
/* Puron lacks forbidden_zone_info and its legacy parser truncates the cache
 * allocation/copy to 4*(count+1). Use the classified-quads protocol directly.
 * This route was verified with original navigation; the exact Puron image is
 * also enabled at the owner's request, pending user hardware feedback. No IPC reads. */
static void zones_native_message(cJSON *zones,unsigned char text[4096]){
 uint32_t count=(uint32_t)cJSON_GetArraySize(zones),size=8+36*count;
 memset(text,0,4096);memcpy(text,&size,4);memcpy(text+4,&count,4);
 for(uint32_t i=0;i<count;i++){
  cJSON *z=cJSON_GetArrayItem(zones,(int)i);
  uint32_t type=(!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(z,"can_clean"))?2u:0u)|(!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(z,"can_mop"))?1u:0u);
  memcpy(text+8+36*i,&type,4);
  for(int j=0;j<4;j++){
   float x=(float)cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(z,"x"),j)->valuedouble;
   float y=-(float)cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(z,"y"),j)->valuedouble;
   memcpy(text+12+36*i+8*j,&x,4);memcpy(text+16+36*i+8*j,&y,4);
  }
 }
}
/* RoboJet's JSON parser uses flat arrays and preserves walls when their keys
 * are absent. Native 0x1d4de uses 32 bytes/quad, 0x1d588 negates Y, and
 * 0x1d506 sends type6; the parser requests type16 readback at 0x1cd3e.
 * Never send classified 36-byte records to this navigation build. */
static char *zones_robojet_payload(cJSON *zones){
 cJSON *o=cJSON_CreateObject();cJSON_AddStringToObject(o,"ProtocolVersion","1.0");cJSON_AddStringToObject(o,"cmd_origin","app");
 cJSON *xs=cJSON_AddArrayToObject(o,"forbidden_zone_x"),*ys=cJSON_AddArrayToObject(o,"forbidden_zone_y"),*z;
 cJSON_ArrayForEach(z,zones){
  if(!cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(z,"can_clean"))||!cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(z,"can_mop"))){cJSON_Delete(o);return NULL;}
  for(int j=0;j<4;j++){cJSON_AddItemToArray(xs,cJSON_Duplicate(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(z,"x"),j),1));cJSON_AddItemToArray(ys,cJSON_Duplicate(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(z,"y"),j),1));}
 }
 char *payload=cJSON_PrintUnformatted(o);cJSON_Delete(o);return payload;
}
static int zones_send(int profile,int q,cJSON *zones,const char *payload){
 if(profile!=1&&profile!=2&&profile!=3)return 1;
 struct {long type;unsigned char text[4096];} message={1,{0}};
 if(profile==2){message.type=6;zones_native_message(zones,message.text);}
 else if(profile==3){char *legacy=zones_robojet_payload(zones);if(!legacy)return 1;size_t n=strlen(legacy);if(n>=sizeof message.text){free(legacy);return 1;}memcpy(message.text,legacy,n);free(legacy);}
 else memcpy(message.text,payload,strlen(payload));
 if(q<0||msgsnd(q,&message,sizeof(message.text),IPC_NOWAIT)){perror("Zones not dispatched");return 1;}

 if(profile==2){
  /* Same one-shot configuration readback as Puron handler at 0x20c7a.
   * Original navigation type16 replies with type17; no periodic polling. */
  memset(message.text,0,sizeof message.text);message.type=16;
  if(msgsnd(q,&message,sizeof(message.text),IPC_NOWAIT)){perror("Zones sent but configuration readback not requested");return 1;}
 }return 0;
}

/* Read configuration once at startup. Type16 never replaces zones; original
 * navigation replies with type17, which initializes the native app cache. */
static int zones_refresh_cache(void){
 key_t ak=ftok("/",1),nk=ftok("/",14);if(ak==(key_t)-1||nk==(key_t)-1)return 1;
 int aq=msgget(ak,0),nq=msgget(nk,0);char app[65],nav[65];
 if(aq<0||nq<0||!zones_receiver_hash(aq,"/root/app_server/wifi_app_server_grit",app)||!zones_profile_hash(app)||!zones_receiver_hash(nq,"/root/hoslam/demo_rplidar",nav)||!zones_refresh_pair(app,nav))return 1;
 struct {long type;unsigned char text[4096];} message={16,{0}};
 if(msgsnd(nq,&message,sizeof message.text,IPC_NOWAIT)){perror("Configuration read not dispatched");return 1;}
 return 0;
}

/* An explicit JSON argument; never a shell command or an IPC consumer. */
int main(int argc,char **argv){
 if(zigma_version_only(argc,argv,"zigma-zones-arm"))return 0;
 if(argc==2&&!strcmp(argv[1],"--refresh-cache"))return zones_refresh_cache();
 int dry=argc==3&&!strcmp(argv[1],"--dry-run");if(argc!=(dry?3:2))return 2;
 const char *end; cJSON *o=cJSON_ParseWithOpts(argv[dry?2:1],&end,1);if(!o)return 2;
 cJSON *zones=cJSON_GetObjectItemCaseSensitive(o,"forbidden_zone_info");int valid=cJSON_IsObject(o)&&cJSON_GetArraySize(o)==3&&cJSON_IsArray(zones)&&cJSON_GetArraySize(zones)<=12;
 cJSON *version=cJSON_GetObjectItemCaseSensitive(o,"ProtocolVersion"),*origin=cJSON_GetObjectItemCaseSensitive(o,"cmd_origin");valid=valid&&cJSON_IsString(version)&&!strcmp(version->valuestring,"1.0")&&cJSON_IsString(origin)&&!strcmp(origin->valuestring,"app");
 cJSON *z;cJSON_ArrayForEach(z,zones){cJSON *x=cJSON_GetObjectItemCaseSensitive(z,"x"),*y=cJSON_GetObjectItemCaseSensitive(z,"y");valid=valid&&cJSON_IsObject(z)&&cJSON_GetArraySize(z)==4&&cJSON_IsArray(x)&&cJSON_IsArray(y)&&cJSON_GetArraySize(x)==4&&cJSON_GetArraySize(y)==4&&cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(z,"can_clean"))&&cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(z,"can_mop"));for(int i=0;i<4;i++){cJSON *a=cJSON_GetArrayItem(x,i),*b=cJSON_GetArrayItem(y,i);if(!cJSON_IsNumber(a)||!cJSON_IsNumber(b)||!isfinite(a->valuedouble)||!isfinite(b->valuedouble)||floor(a->valuedouble)!=a->valuedouble||floor(b->valuedouble)!=b->valuedouble||fabs(a->valuedouble)>20000||fabs(b->valuedouble)>20000)valid=0;}}
 char *payload=valid?cJSON_PrintUnformatted(o):NULL;if(!payload||strlen(payload)>=4096){cJSON_Delete(o);free(payload);return 2;}
 if(dry){puts(payload);cJSON_Delete(o);free(payload);return 0;}
 key_t key=ftok("/",1);int q=key==(key_t)-1?-1:msgget(key,0);char hash[65];
 int profile=q>=0&&zones_receiver_hash(q,"/root/app_server/wifi_app_server_grit",hash)?zones_profile_hash(hash):0;
 if(!profile){fprintf(stderr,"Unsupported app-server identity; zones not dispatched\n");cJSON_Delete(o);free(payload);return 1;}
 if(profile==2||profile==3){
  key=ftok("/",14);q=key==(key_t)-1?-1:msgget(key,0);
  int nq=q;
  if(nq<0||!zones_receiver_hash(nq,"/root/hoslam/demo_rplidar",hash)||(profile==3?strcmp(hash,"f793e93abb85a1a7b1cbe8cec1f53b2c6d9aac7c21c8fb3b956f460fe693f88c")!=0:!zones_navigation_hash(hash))){
   fprintf(stderr,"Unsupported navigation identity; zones not dispatched\n");cJSON_Delete(o);free(payload);return 1;
  }
 }
 if(profile==3){key=ftok("/",1);q=key==(key_t)-1?-1:msgget(key,0);if(q<0||!zones_receiver_hash(q,"/root/app_server/wifi_app_server_grit",hash)||zones_profile_hash(hash)!=3){cJSON_Delete(o);free(payload);return 1;}}
 int rc=zones_send(profile,q,zones,payload);cJSON_Delete(o);free(payload);return rc;
}
