#ifndef ZIGMA_NATIVE_IDENTITY_H
#define ZIGMA_NATIVE_IDENTITY_H
#include <stdio.h>
#include <sys/stat.h>
/* Ciccia exposes the same native files through firmware symlinks. Compare the
 * executable inode, not a path spelling; copies/replaced/deleted images fail. */
static inline int zigma_same_file(const char *a,const char *b){
 struct stat x,y;return !stat(a,&x)&&!stat(b,&y)&&S_ISREG(x.st_mode)&&S_ISREG(y.st_mode)&&x.st_dev==y.st_dev&&x.st_ino==y.st_ino;
}
static inline int zigma_native_process(int pid,const char *expected){
 if(pid<=0)return 0;
 char path[64];snprintf(path,sizeof path,"/proc/%d/exe",pid);return zigma_same_file(path,expected);
}
#endif
