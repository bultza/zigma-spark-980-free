/* Transport only. No cloud, commands, JSON interpretation or process supervision. */
#define _POSIX_C_SOURCE 200809L
#include "zigma_local_ipc.h"
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>

union zlocal_semun {int val;struct semid_ds *buf;unsigned short *array;};
struct message {long type;char text[ZLOCAL_MESSAGE_MAX+1];};
static void empty(struct zlocal_ipc *ipc){
 ipc->request_id=ipc->response_id=ipc->map_id=ipc->sem_id=-1;ipc->map=NULL;
}
int zlocal_attach(struct zlocal_ipc *ipc,int request,int response,int map,int sem){
 empty(ipc);struct msqid_ds q;struct shmid_ds s;struct semid_ds d;
 union zlocal_semun arg;arg.buf=&d;
 if(msgctl(request,IPC_STAT,&q)||msgctl(response,IPC_STAT,&q)||shmctl(map,IPC_STAT,&s)||semctl(sem,0,IPC_STAT,arg)<0)return -1;
 if(s.shm_segsz!=ZLOCAL_MAP_BYTES||d.sem_nsems!=2){errno=EPROTO;return -1;}
 const void *memory=shmat(map,NULL,SHM_RDONLY);if(memory==(void*)-1)return -1;
 ipc->request_id=request;ipc->response_id=response;ipc->map_id=map;ipc->sem_id=sem;ipc->map=memory;return 0;
}
int zlocal_attach_keys(struct zlocal_ipc *ipc,const char *path){
 empty(ipc);key_t keys[4];
 for(int i=0;i<4;i++){keys[i]=ftok(path,i+1);if(keys[i]==(key_t)-1)return -1;}
 int request=msgget(keys[0],0);if(request<0)return -1;
 int response=msgget(keys[1],0);if(response<0)return -1;
 int map=shmget(keys[2],1,0);if(map<0)return -1;
 int sem=semget(keys[3],0,0);if(sem<0)return -1;
 return zlocal_attach(ipc,request,response,map,sem);
}
void zlocal_detach(struct zlocal_ipc *ipc){if(ipc->map)shmdt(ipc->map);empty(ipc);}
enum zlocal_result zlocal_receive(struct zlocal_ipc *ipc,long *type,char *text,size_t cap,size_t *length){
 if(!type||!text||!length||cap<ZLOCAL_MESSAGE_MAX+1){errno=EINVAL;return ZLOCAL_ERROR;}
 *length=0;text[0]=0;*type=0;struct message msg;
 ssize_t n=msgrcv(ipc->response_id,&msg,ZLOCAL_MESSAGE_MAX,0,IPC_NOWAIT);
 if(n<0){
  if(errno==ENOMSG||errno==EINTR)return ZLOCAL_EMPTY;
  if(errno!=E2BIG)return ZLOCAL_ERROR;
  /* Discard one oversized head instead of wedging all following responses.
   * Exclusive consumption is a prerequisite; truncation is never delivered. */
  n=msgrcv(ipc->response_id,&msg,ZLOCAL_MESSAGE_MAX,0,IPC_NOWAIT|MSG_NOERROR);
  if(n<0)return errno==ENOMSG||errno==EINTR?ZLOCAL_EMPTY:ZLOCAL_ERROR;
  return ZLOCAL_DROPPED;
 }
 size_t size=(size_t)n;
 /* app-server send_msg_to_wifi_app always sends a zero-padded 4096-byte body.
  * Also accept the client's variable-length framing; never ignore nonzero data
  * hidden after an embedded NUL. */
 while(size&&msg.text[size-1]==0)size--;
 if(!size||memchr(msg.text,0,size))return ZLOCAL_DROPPED;
 memcpy(text,msg.text,size);text[size]=0;*length=size;*type=msg.type;return ZLOCAL_READY;
}
enum zlocal_result zlocal_send(struct zlocal_ipc *ipc,long type,const char *text,size_t length){
 if(type<=0||!text||!length||length>ZLOCAL_MESSAGE_MAX){errno=EINVAL;return ZLOCAL_ERROR;}
 struct message msg;msg.type=type;memcpy(msg.text,text,length);
 if(msgsnd(ipc->request_id,&msg,length,IPC_NOWAIT)==0)return ZLOCAL_READY;
 return errno==EAGAIN||errno==EINTR?ZLOCAL_EMPTY:ZLOCAL_ERROR;
}
enum zlocal_result zlocal_receive_map(struct zlocal_ipc *ipc,char *text,size_t cap,size_t *length){
 if(!ipc->map||!text||!length||cap<ZLOCAL_MAP_BYTES+1){errno=EINVAL;return ZLOCAL_ERROR;}
 *length=0;text[0]=0;
 /* Undo only the reader reservation if killed before release. Producer remains
  * blocked throughout the copy. The atomic release cancels the undo adjustment
  * before transferring ownership; no pending undo can affect a later writer. */
 sigset_t block,previous;sigemptyset(&block);sigaddset(&block,SIGTERM);sigaddset(&block,SIGINT);
 if(sigprocmask(SIG_BLOCK,&block,&previous))return ZLOCAL_ERROR;
 struct sembuf acquire={1,-1,IPC_NOWAIT|SEM_UNDO};
 if(semop(ipc->sem_id,&acquire,1)<0){int error=errno;sigprocmask(SIG_SETMASK,&previous,NULL);errno=error;
  return error==EAGAIN||error==EINTR?ZLOCAL_EMPTY:ZLOCAL_ERROR;}
 memcpy(text,ipc->map,ZLOCAL_MAP_BYTES);
 struct sembuf release[]={{1,1,IPC_NOWAIT|SEM_UNDO},{1,-1,IPC_NOWAIT},{0,1,IPC_NOWAIT}};int result;
 do{result=semop(ipc->sem_id,release,3);}while(result<0&&errno==EINTR);
 int error=errno;sigprocmask(SIG_SETMASK,&previous,NULL);errno=error;
 if(result<0)return ZLOCAL_ERROR;
 const char *nul=memchr(text,0,ZLOCAL_MAP_BYTES);
 if(!nul){text[0]=0;return ZLOCAL_DROPPED;}
 size_t n=(size_t)(nul-text);if(!n)return ZLOCAL_DROPPED;
 text[n]=0;*length=n;return ZLOCAL_READY;
}
