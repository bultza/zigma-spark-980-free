#define _POSIX_C_SOURCE 200809L
#include "zigma_version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/msg.h>
/* Upper local protocol captured from this app-server branch. No queue reads. */
int main(int argc,char **argv){
 if(zigma_version_only(argc,argv,"zigma-area-arm"))return 0;
 int dry=argc==6&&!strcmp(argv[1],"--dry-run"),start=dry?2:1;
 if(argc!=start+4){fprintf(stderr,"Usage: zigma-area [--dry-run] X_MIN Y_MIN X_MAX Y_MAX\n");return 2;}
 long p[4];for(int i=0;i<4;i++){char *end;errno=0;p[i]=strtol(argv[start+i],&end,10);if(errno||end==argv[start+i]||*end||p[i]<-20000||p[i]>20000)return 2;}
 if(p[0]>=p[2]||p[1]>=p[3])return 2;
 struct {long type;char text[4096];} message={1,{0}};
 snprintf(message.text,sizeof(message.text),"{\"ProtocolVersion\":\"1.0\",\"WorkStatus\":\"PlanningRect\",\"planning_rect_x\":[%ld,%ld,%ld,%ld],\"planning_rect_y\":[%ld,%ld,%ld,%ld],\"cmd_origin\":\"app\"}",p[0],p[0],p[2],p[2],p[1],p[3],p[3],p[1]);
 if(dry){puts(message.text);return 0;}
 key_t key=ftok("/",1);int q=key==(key_t)-1?-1:msgget(key,0);
 if(q<0||msgsnd(q,&message,sizeof(message.text),IPC_NOWAIT)){perror("Local area command not dispatched");return 1;}
 return 0;
}
