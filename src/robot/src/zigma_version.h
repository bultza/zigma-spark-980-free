#ifndef ZIGMA_VERSION_H
#define ZIGMA_VERSION_H
#include <stdio.h>
#include <string.h>
#include "zigma_versions_generated.h"
/* Must run before configuration, IPC, locks, network or filesystem writes. */
static int zigma_version_only(int argc,char **argv,const char *name){
 if(argc!=2||(strcmp(argv[1],"-v")&&strcmp(argv[1],"--version")))return 0;
 const char *version="unknown";
 for(unsigned i=0;i<sizeof zigma_versions/sizeof zigma_versions[0];i++)if(!strcmp(name,zigma_versions[i].name)){version=zigma_versions[i].version;break;}
 printf("%s %s (built %s)\n",name,version,__DATE__ " " __TIME__ " UTC");return 1;
}
#endif
