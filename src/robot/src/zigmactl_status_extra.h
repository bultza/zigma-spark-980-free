#ifndef ZIGMACTL_STATUS_EXTRA_H
#define ZIGMACTL_STATUS_EXTRA_H
#include <ifaddrs.h>
#include <arpa/inet.h>
#ifndef ZIGMACTL_WIRELESS_PATH
#define ZIGMACTL_WIRELESS_PATH "/proc/net/wireless"
#endif
#ifndef ZIGMACTL_CARRIER_PATH
#define ZIGMACTL_CARRIER_PATH "/sys/class/net/wlan0/carrier"
#endif
#ifndef ZIGMACTL_MAP_PATH
#define ZIGMACTL_MAP_PATH "/tmp/zigma-free/observation/map.json"
#endif
/* Passive WLAN signal read: no scan, subprocess or vendor IPC. */
static bool wifi_signal_read(const char *wireless, const char *carrier, int *dbm){
 FILE *f=fopen(carrier,"re");int connected=0;
 if(!f)return false;
 int ok=fscanf(f,"%d",&connected);fclose(f);
 if(ok!=1||connected!=1)return false;
 f=fopen(wireless,"re");if(!f)return false;
 char line[512],iface[32];unsigned flags;double quality,level;bool found=false;
 while(fgets(line,sizeof line,f)){
  if(sscanf(line," %31[^:]: %x %lf %lf",iface,&flags,&quality,&level)==4&&
     !strcmp(iface,"wlan0")&&isfinite(level)&&level>=-127&&level<0){
   *dbm=(int)level;found=true;break;
  }
 }
 fclose(f);return found;
}
static cJSON *ctl_wifi(double epoch){
 cJSON *w=cJSON_CreateObject();int carrier=-1,dbm=0;FILE *f=fopen(ZIGMACTL_CARRIER_PATH,"r");
 if(f){if(fscanf(f,"%d",&carrier)!=1)carrier=-1;fclose(f);}
 bool signal=wifi_signal_read(ZIGMACTL_WIRELESS_PATH,ZIGMACTL_CARRIER_PATH,&dbm);
 cJSON_AddStringToObject(w,"interface","wlan0");
 cJSON_AddBoolToObject(w,"available",carrier==0||carrier==1);
 if(carrier==0||carrier==1)cJSON_AddBoolToObject(w,"connected",carrier==1);else cJSON_AddNullToObject(w,"connected");
 if(signal)cJSON_AddNumberToObject(w,"signal_dbm",dbm);else cJSON_AddNullToObject(w,"signal_dbm");
 char ip[INET_ADDRSTRLEN]="";struct ifaddrs *list=NULL;
 if(carrier==1&&!getifaddrs(&list)){
  for(struct ifaddrs *a=list;a;a=a->ifa_next)if(a->ifa_addr&&a->ifa_addr->sa_family==AF_INET&&!strcmp(a->ifa_name,"wlan0")){
   inet_ntop(AF_INET,&((struct sockaddr_in*)a->ifa_addr)->sin_addr,ip,sizeof(ip));break;
  }
  freeifaddrs(list);
 }
 if(*ip)cJSON_AddStringToObject(w,"ipv4",ip);else cJSON_AddNullToObject(w,"ipv4");
 cJSON_AddNumberToObject(w,"observed_at",epoch);return w;
}
#include "zigma_position.h"
static cJSON *ctl_position(cJSON *in,double epoch){
 cJSON *map=nav_read(ZIGMACTL_MAP_PATH),*o=position_select(in,map,epoch);cJSON_Delete(map);return o;
}
#endif
