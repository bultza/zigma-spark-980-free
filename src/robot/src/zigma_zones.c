#define _POSIX_C_SOURCE 200809L
#include "zigma_version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include "cJSON.h"
/* An explicit JSON argument; never a shell command or an IPC consumer. */
int main(int argc,char **argv){
 if(zigma_version_only(argc,argv,"zigma-zones-arm"))return 0;
 int dry=argc==3&&!strcmp(argv[1],"--dry-run");if(argc!=(dry?3:2))return 2;
 const char *end; cJSON *o=cJSON_ParseWithOpts(argv[dry?2:1],&end,1);if(!o)return 2;
 cJSON *zones=cJSON_GetObjectItemCaseSensitive(o,"forbidden_zone_info");int valid=cJSON_IsObject(o)&&cJSON_GetArraySize(o)==3&&cJSON_IsArray(zones)&&cJSON_GetArraySize(zones)<=12;
 cJSON *version=cJSON_GetObjectItemCaseSensitive(o,"ProtocolVersion"),*origin=cJSON_GetObjectItemCaseSensitive(o,"cmd_origin");valid=valid&&cJSON_IsString(version)&&!strcmp(version->valuestring,"1.0")&&cJSON_IsString(origin)&&!strcmp(origin->valuestring,"app");
 cJSON *z;cJSON_ArrayForEach(z,zones){cJSON *x=cJSON_GetObjectItemCaseSensitive(z,"x"),*y=cJSON_GetObjectItemCaseSensitive(z,"y");valid=valid&&cJSON_IsObject(z)&&cJSON_GetArraySize(z)==4&&cJSON_IsArray(x)&&cJSON_IsArray(y)&&cJSON_GetArraySize(x)==4&&cJSON_GetArraySize(y)==4&&cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(z,"can_clean"))&&cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(z,"can_mop"));for(int i=0;i<4;i++){cJSON *a=cJSON_GetArrayItem(x,i),*b=cJSON_GetArrayItem(y,i);if(!cJSON_IsNumber(a)||!cJSON_IsNumber(b)||!isfinite(a->valuedouble)||!isfinite(b->valuedouble)||floor(a->valuedouble)!=a->valuedouble||floor(b->valuedouble)!=b->valuedouble||fabs(a->valuedouble)>20000||fabs(b->valuedouble)>20000)valid=0;}}
 char *payload=valid?cJSON_PrintUnformatted(o):NULL;cJSON_Delete(o);if(!payload||strlen(payload)>=4096){free(payload);return 2;}
 if(dry){puts(payload);free(payload);return 0;}
 struct {long type;char text[4096];} message={1,{0}};memcpy(message.text,payload,strlen(payload));free(payload);
 key_t key=ftok("/",1);int q=key==(key_t)-1?-1:msgget(key,0);if(q<0||msgsnd(q,&message,sizeof(message.text),IPC_NOWAIT)){perror("Zones not dispatched");return 1;}return 0;
}
