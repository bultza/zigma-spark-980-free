#define _POSIX_C_SOURCE 200809L
#include "zigma_version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
/* Caccamora 4cf07099: parser table at 0x2b3e0. Type 4 handles
 * Volume/over_ridge; type 1 handles ContinueCleaning. No queue reads. */
int main(int argc,char **argv){
 if(zigma_version_only(argc,argv,"zigma-options-arm"))return 0;
 int dry=argc==4&&!strcmp(argv[1],"--dry-run"),i=dry?2:1;
 if(argc!=i+2)return 2;
 const char *key=argv[i],*value=argv[i+1],*native=NULL;long type=4;
 if(!strcmp(key,"volume")){char *end;long n=strtol(value,&end,10);if(!*value||*end||n<0||n>100)return 2;native="Volume";}
 else if(!strcmp(key,"over_ridge")||!strcmp(key,"continue_clean")){
  if(strcmp(value,"true")&&strcmp(value,"false"))return 2;
  native=!strcmp(key,"over_ridge")?"over_ridge":"ContinueCleaning";if(!strcmp(key,"continue_clean"))type=1;
 }else return 2;
 struct {long type;char text[4096];} message={type,{0}};
 snprintf(message.text,sizeof(message.text),"{\"ProtocolVersion\":\"1.0\",\"%s\":%s,\"cmd_origin\":\"app\"}",native,value);
 if(dry){printf("%ld %s\n",type,message.text);return 0;}
 key_t keyid=ftok("/",1);int q=keyid==(key_t)-1?-1:msgget(keyid,0);
 if(q<0||msgsnd(q,&message,sizeof(message.text),IPC_NOWAIT)){perror("Setting not dispatched");return 1;}return 0;
}
