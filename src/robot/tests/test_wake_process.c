/* Host syscall shim: emulate bind with links in a disposable directory.
 * This validates process recovery, not kernel mount behavior on a robot. */
#define _POSIX_C_SOURCE 200809L
#define main control_main
#define mount fake_mount
#define umount2 fake_umount2
#define msgget fake_msgget
#define msgsnd fake_msgsnd
#define geteuid fake_geteuid
#define fstat fake_fstat
#include "../src/zigmactl.c"
#undef main
#undef geteuid
#undef fstat
extern int fstat(int fd, struct stat *st);
uid_t fake_geteuid(void){return 0;}
int fake_fstat(int fd,struct stat *st){int rc=fstat(fd,st);if(!rc)st->st_uid=0;return rc;}
int fake_mount(const char *source,const char *target,const char *type,unsigned long flags,const void *data){
 (void)type;(void)flags;(void)data;char save[PATH_MAX];snprintf(save,sizeof save,"%s.saved",target);
 if(rename(target,save))return -1;
 if(link(source,target)){rename(save,target);return -1;}return 0;
}
int fake_umount2(const char *target,int flags){
 (void)flags;char save[PATH_MAX];snprintf(save,sizeof save,"%s.saved",target);
 if(unlink(target))return -1;
 return rename(save,target);
}
int fake_msgget(key_t key,int flags){(void)key;(void)flags;return 9;}
int fake_msgsnd(int q,const void *p,size_t size,int flags){
 (void)q;(void)size;(void)flags;const struct zigma_msg *m=p;
 FILE *f=fopen(WAKE_TEST_LOG,"a");if(!f)return -1;fprintf(f,"%u %u\n",m->data[7],m->data[8]);fclose(f);return 0;
}
int main(int argc,char **argv){return control_main(argc,argv);}
