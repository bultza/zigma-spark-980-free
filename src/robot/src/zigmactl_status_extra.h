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
static cJSON *ctl_position(cJSON *in,double epoch){
 cJSON *p=nv(in,"navigation_pose"),*map=nav_read(ZIGMACTL_MAP_PATH),*o=cJSON_CreateObject();
 double x=nn(p,"x"),y=nn(p,"y"),heading=nn(p,"heading"),res=nn(p,"resolution_m"),age=epoch-nn(p,"cache_read_at");
 bool ok=cJSON_IsObject(p)&&cJSON_IsObject(map)&&!strcmp(ns(p,"kind"),"navigation_pose_cache")&&!strcmp(ns(p,"frame"),"map_grid")&&*ns(p,"map_id")&&isfinite(x)&&isfinite(y)&&isfinite(heading)&&fabs(heading)<=3.141593&&isfinite(res)&&res>=0.001&&res<=1&&isfinite(age)&&age>=0&&age<=15&&x>=0&&y>=0&&x<nn(p,"width")&&y<nn(p,"height");
 const char *keys[]={"map_id","width","height","map_origin",NULL};
 for(int i=0;keys[i];i++)if(!nv(p,keys[i])||!nv(map,keys[i])||!cJSON_Compare(nv(p,keys[i]),nv(map,keys[i]),1))ok=false;
 cJSON *origin=nv(p,"map_origin");
 if(cJSON_GetArraySize(origin)!=2||!cJSON_IsNumber(cJSON_GetArrayItem(origin,0))||!cJSON_IsNumber(cJSON_GetArrayItem(origin,1)))ok=false;
 cJSON_AddBoolToObject(o,"available",ok);cJSON_AddBoolToObject(o,"fresh",false);
 cJSON_AddStringToObject(o,"quality",ok?"cached":"unavailable");
 cJSON_AddStringToObject(o,"frame","map_grid");cJSON_AddStringToObject(o,"units","cells");cJSON_AddStringToObject(o,"heading_units","radians");
 const char *fields[]={"x","y","map_id","width","height","map_origin","cache_read_at","resolution_m",NULL};
 for(int i=0;fields[i];i++)if(ok)cJSON_AddItemToObject(o,fields[i],cJSON_Duplicate(nv(p,fields[i]),1));else cJSON_AddNullToObject(o,fields[i]);
 if(ok)cJSON_AddNumberToObject(o,"heading",heading);else cJSON_AddNullToObject(o,"heading");
 cJSON_AddNullToObject(o,"measured_at");cJSON_Delete(map);return o;
}
#endif
