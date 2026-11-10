#ifndef ZIGMA_PROCESS_H
#define ZIGMA_PROCESS_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
static unsigned long long zigma_start_ticks(pid_t pid){
 if(pid<=0)return 0;
 char path[64],line[4096];snprintf(path,sizeof path,"/proc/%ld/stat",(long)pid);
 FILE *f=fopen(path,"r");if(!f)return 0;
 char *ok=fgets(line,sizeof line,f);fclose(f);if(!ok)return 0;
 char *p=strrchr(line,')');if(!p)return 0;p+=2;
 for(int field=3;field<22;field++){p=strchr(p,' ');if(!p)return 0;p++;}
 return strtoull(p,NULL,10);
}
#endif
