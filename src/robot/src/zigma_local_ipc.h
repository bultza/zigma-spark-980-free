#ifndef ZIGMA_LOCAL_IPC_H
#define ZIGMA_LOCAL_IPC_H
#include <stddef.h>
#include <sys/types.h>

#ifndef ZLOCAL_MESSAGE_MAX
#define ZLOCAL_MESSAGE_MAX 8192
#endif
#define ZLOCAL_MAP_BYTES 204800
enum zlocal_result {ZLOCAL_ERROR=-1,ZLOCAL_EMPTY=0,ZLOCAL_READY=1,ZLOCAL_DROPPED=2};
struct zlocal_ipc {int request_id,response_id,map_id,sem_id;const char *map;};
/* The caller must establish sole consumer ownership before attachment. No resource
 * creation, initialization, reset, queue clearing or IPC_RMID occurs here. */
int zlocal_attach(struct zlocal_ipc *ipc,int request,int response,int map,int sem);
int zlocal_attach_keys(struct zlocal_ipc *ipc,const char *ftok_path);
void zlocal_detach(struct zlocal_ipc *ipc);
enum zlocal_result zlocal_receive(struct zlocal_ipc *ipc,long *type,char *text,size_t cap,size_t *length);
enum zlocal_result zlocal_receive_map(struct zlocal_ipc *ipc,char *text,size_t cap,size_t *length);
enum zlocal_result zlocal_send(struct zlocal_ipc *ipc,long type,const char *text,size_t length);
#endif
