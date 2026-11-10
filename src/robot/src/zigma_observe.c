#define _FILE_OFFSET_BITS 64
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
#include "zigma_version.h"
#include "zigma_native_identity.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include "cJSON.h"

/* Coexistence adapter: no queue reads or semaphore operations. Optional info-only request. */
static volatile sig_atomic_t running=1;
static int cache_pid=0,nav_pid=0;
static cJSON *map_geometry=NULL;
static bool map_requests=false;
static char *last_path_signature=NULL;
static time_t path_changed_at=0;
static void stop(int n){(void)n;running=0;}
static cJSON *get(cJSON *o,const char *key){return cJSON_GetObjectItemCaseSensitive(o,key);}
static int number(cJSON *o,const char *key){cJSON *v=get(o,key);return cJSON_IsNumber(v)?v->valueint:0;}
static bool save(const char *dir,const char *name,cJSON *o){
 char dest[1024],temp[1100];snprintf(dest,sizeof dest,"%s/%s",dir,name);snprintf(temp,sizeof temp,"%s.%ld.tmp",dest,(long)getpid());
 char *s=cJSON_PrintUnformatted(o);if(!s)return false;
 int fd=open(temp,O_WRONLY|O_CREAT|O_EXCL,0600);bool ok=fd>=0;size_t off=0,len=strlen(s);
 while(ok&&off<len){ssize_t n=write(fd,s+off,len-off);if(n<=0)ok=false;else off+=(size_t)n;}
 if(fd>=0){if(fsync(fd))ok=false;if(close(fd))ok=false;}
 if(ok&&rename(temp,dest))ok=false;
 if(!ok)unlink(temp);
 free(s);return ok;
}
static time_t log_time(const char *line){
 struct tm wall={0};if(!strptime(line,"[%Y-%m-%d %H:%M:%S",&wall))return 0;
 /* An unzoned repeated autumn-hour log cannot prove freshness. */
 struct tm a=wall,b=wall;a.tm_isdst=0;b.tm_isdst=1;time_t ta=mktime(&a),tb=mktime(&b);
 if(ta!=tb&&a.tm_hour==wall.tm_hour&&b.tm_hour==wall.tm_hour&&a.tm_mday==wall.tm_mday&&b.tm_mday==wall.tm_mday)return 0;
 wall.tm_isdst=-1;return mktime(&wall);
}
static const char *vendor_state(int code){
 switch(code){case 14:return "SpotClean";case 12:case 11:return "AutoClean";case 1:case 2:return "Standby";case 22:return "BackCharging";case 4:return "Hibernating";case 30:return "PileCharging";case 31:return "DirCharging";case 3:return "Malfunction";case 21:return "DirectionControl";case 20:return "PlanningLocation";case 13:return "PlanningRect";case 50:return "LocationAlarm";case 15:return "SelectClean";case 70:return "CleanDone";default:return "unknown";}
}
static bool request_info(void){
 key_t key=ftok("/",1);if(key==(key_t)-1)return false;int q=msgget(key,0);if(q<0)return false;
 struct {long type;char text[64];} message={8,"{\"ProtocolVersion\":\"1.0\"}"};
 return msgsnd(q,&message,strlen(message.text)+1,IPC_NOWAIT)==0;
}
static cJSON *read_status(const char *file){
 cJSON *o=cJSON_CreateObject();cJSON_AddStringToObject(o,"working_status","unknown");cJSON_AddStringToObject(o,"error","unknown");cJSON_AddNumberToObject(o,"observed_at",0);cJSON_AddStringToObject(o,"telemetry_source","app_server_log");
 FILE *f=fopen(file,"rb");if(!f)return o;
 if(fseek(f,0,SEEK_END)){fclose(f);return o;}long end=ftell(f);if(end<0){fclose(f);return o;}long start=end>262144?end-262144:0;
 if(fseek(f,start,SEEK_SET)){fclose(f);return o;}char line[2048],last[2048]={0};if(start&&!fgets(line,sizeof line,f)){fclose(f);return o;}
 time_t finished=0;
 while(fgets(line,sizeof line,f)){if(strstr(line,"state:")&&strstr(line," battery:")&&strstr(line," errno:"))snprintf(last,sizeof last,"%s",line);
  char *signal=strstr(line,"set_clean_finish_state = ");int flag=0;if(signal&&sscanf(signal,"set_clean_finish_state = %d",&flag)==1&&flag==1){time_t at=log_time(line);if(at>finished)finished=at;}
 }
 if(finished)cJSON_AddNumberToObject(o,"clean_finished_at",finished);
 fclose(f);char *p=strstr(last,"state:");int state,battery,area,elapsed,error;
 if(p&&sscanf(p,"state:%d battery:%d area:%d time:%d errno:%d",&state,&battery,&area,&elapsed,&error)==5&&battery>=0&&battery<=100){
  cJSON_ReplaceItemInObjectCaseSensitive(o,"observed_at",cJSON_CreateNumber((double)log_time(last)));
  cJSON_AddNumberToObject(o,"battery_level",battery);cJSON_AddNumberToObject(o,"vendor_state_code",state);cJSON_AddNumberToObject(o,"vendor_error_code",error);
  cJSON_AddNumberToObject(o,"vendor_clean_area_raw",area);cJSON_AddNumberToObject(o,"vendor_clean_time_raw",elapsed);
  if(!error)cJSON_ReplaceItemInObjectCaseSensitive(o,"error",cJSON_CreateString("NoError"));
  cJSON_ReplaceItemInObjectCaseSensitive(o,"working_status",cJSON_CreateString(vendor_state(state)));
  /* Log time/area units remain raw until independently checked. */
 }
 return o;
}
static unsigned char *decode(const cJSON *v,size_t *size){
 if(!cJSON_IsString(v))return NULL;
 static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
 const char *s=v->valuestring;size_t len=strlen(s);if(len%4||len>1000000)return NULL;
 unsigned char *out=malloc(len/4*3+1);if(!out)return NULL;size_t n=0;
 for(size_t i=0;i<len;i+=4){unsigned a[4];for(int j=0;j<4;j++){const char *p=strchr(alphabet,s[i+j]);
   if(s[i+j]=='=')a[j]=0;else if(s[i+j]&&p)a[j]=(unsigned)(p-alphabet);else{free(out);return NULL;}}
  if(s[i]=='='||s[i+1]=='='||(s[i+2]=='='&&s[i+3]!='=')||((s[i+2]=='='||s[i+3]=='=')&&i+4!=len)){free(out);return NULL;}
  out[n++]=(unsigned char)((a[0]<<2)|(a[1]>>4));if(s[i+2]!='=')out[n++]=(unsigned char)((a[1]<<4)|(a[2]>>2));if(s[i+3]!='=')out[n++]=(unsigned char)((a[2]<<6)|a[3]);
 }*size=n;return out;
}
static void point(cJSON *o,const char *key,double x,double y){cJSON *p=cJSON_CreateObject();cJSON_AddNumberToObject(p,"x",x);cJSON_AddNumberToObject(p,"y",y);cJSON_AddItemToObject(o,key,p);}
static bool memory_read(int fd,uint32_t address,void *data,size_t n){return address>=0x10000&&address<0xc0000000&&n<=1048576&&pread(fd,data,n,address)==(ssize_t)n;}
/* Validated only for Caccamora's 4cf07099 app-server build. */
static void replace_value(cJSON *o,const char *key,cJSON *value){cJSON_DeleteItemFromObjectCaseSensitive(o,key);cJSON_AddItemToObject(o,key,value);}
#include "zigma_core_observation.h"
static cJSON *cached_navigation_pose(int fd,const cJSON *geometry){
 /* CommVSLAM singleton/GOT and pose getter, nav build b10fc7b4 only. */
 uint32_t base=0,again=0,vtable=0,vagain=0;unsigned char a[40],b[40],ma[28],mb[28];
 if(!geometry||!memory_read(fd,0x444594,&base,4)||base!=0x45a200||!memory_read(fd,base,&vtable,4)||vtable!=0x43b4f4)return NULL;
 if(!memory_read(fd,base+0x1120,a,sizeof(a))||!memory_read(fd,base+0xe44,ma,sizeof(ma))||!memory_read(fd,base+0x1120,b,sizeof(b))||!memory_read(fd,base+0xe44,mb,sizeof(mb))||memcmp(a,b,sizeof(a))||memcmp(ma,mb,sizeof(ma))||!memory_read(fd,0x444594,&again,4)||again!=base||!memory_read(fd,base,&vagain,4)||vagain!=vtable)return NULL;
 if(a[0]!=1||a[1]!=1)return NULL;
 float pose[3],origin[2],resolution;uint32_t dimensions[2];memcpy(pose,a+28,12);memcpy(origin,ma+8,8);memcpy(dimensions,ma+16,8);memcpy(&resolution,ma+24,4);
 cJSON *grid_origin=get((cJSON*)geometry,"map_origin"),*ox=cJSON_GetArrayItem(grid_origin,0),*oy=cJSON_GetArrayItem(grid_origin,1),*id=get((cJSON*)geometry,"map_id");
 if(!cJSON_IsNumber(ox)||!cJSON_IsNumber(oy)||!cJSON_IsString(id)||!id->valuestring[0]||dimensions[0]!=(uint32_t)number((cJSON*)geometry,"width")||dimensions[1]!=(uint32_t)number((cJSON*)geometry,"height")||!isfinite(resolution)||resolution<0.001||resolution>1||!isfinite(origin[0])||!isfinite(origin[1])||fabs(origin[0]-ox->valuedouble)>1||fabs(dimensions[1]-origin[1]-1-oy->valuedouble)>1)return NULL;
 if(!isfinite(pose[0])||!isfinite(pose[1])||!isfinite(pose[2])||fabs(pose[0])>1000||fabs(pose[1])>1000||fabs(pose[2])>100)return NULL;
 double x=ox->valuedouble+pose[0]/resolution,y=oy->valuedouble-pose[1]/resolution;
 if(x<0||y<0||x>=dimensions[0]||y>=dimensions[1])return NULL;
 cJSON *o=cJSON_Duplicate(geometry,1);cJSON_AddNumberToObject(o,"x",x);cJSON_AddNumberToObject(o,"y",y);cJSON_AddNumberToObject(o,"heading",atan2(-sin(pose[2]),cos(pose[2])));cJSON_AddNumberToObject(o,"resolution_m",resolution);
 cJSON_AddStringToObject(o,"kind","navigation_pose_cache");cJSON_AddStringToObject(o,"frame","map_grid");cJSON_AddNumberToObject(o,"cache_read_at",time(NULL));cJSON_AddNullToObject(o,"measured_at");cJSON_AddBoolToObject(o,"live",false);return o;
}
static void enrich_navigation_pose(cJSON *status){
 if(!nav_pid||!map_geometry)return;
 char path[128];
 if(!zigma_native_process(nav_pid,"/root/hoslam/demo_rplidar"))return;
 snprintf(path,sizeof(path),"/proc/%d/mem",nav_pid);int fd=open(path,O_RDONLY);if(fd<0)return;
 cJSON *pose=cached_navigation_pose(fd,map_geometry);close(fd);if(pose)cJSON_AddItemToObject(status,"navigation_pose",pose);
}
static void remember_map_geometry(cJSON *map){
 cJSON_Delete(map_geometry);map_geometry=cJSON_CreateObject();
 for(const char **k=(const char*[]){"map_id","width","height","map_origin",NULL};*k;k++){cJSON *v=get(map,*k);if(v)cJSON_AddItemToObject(map_geometry,*k,cJSON_Duplicate(v,1));}
}
static bool cached_status(cJSON *status,int fd){
 uint32_t initialized=0,again=0;int32_t a[25],b[25];
 if(!memory_read(fd,0x49d30,&initialized,4)||initialized!=1||!memory_read(fd,0x48e24,a,sizeof(a))||!memory_read(fd,0x48e24,b,sizeof(b))||memcmp(a,b,sizeof(a))||!memory_read(fd,0x49d30,&again,4)||again!=1)return false;
 int state=a[0],battery=a[3],area=a[9],minutes=a[10],error=a[11];
 if(!strcmp(vendor_state(state),"unknown")||battery<0||battery>100||area<0||area>100000||minutes<0||minutes>10080||error<0||error>255)return false;
 bool matching=number(status,"vendor_state_code")==state&&number(status,"battery_level")==battery&&number(status,"vendor_clean_area_raw")==area&&number(status,"vendor_clean_time_raw")==minutes&&number(status,"vendor_error_code")==error;
 if(!matching)replace_value(status,"observed_at",cJSON_CreateNumber(0));
 replace_value(status,"working_status",cJSON_CreateString(vendor_state(state)));
 char err[40];snprintf(err,sizeof(err),"VendorError%d",error);replace_value(status,"error",cJSON_CreateString(error?err:"NoError"));
 replace_value(status,"vendor_state_code",cJSON_CreateNumber(state));replace_value(status,"battery_level",cJSON_CreateNumber(battery));
 replace_value(status,"vendor_error_code",cJSON_CreateNumber(error));replace_value(status,"vendor_clean_area_raw",cJSON_CreateNumber(area));replace_value(status,"vendor_clean_time_raw",cJSON_CreateNumber(minutes));
 replace_value(status,"clean_area_m2",cJSON_CreateNumber(area));replace_value(status,"clean_time_s",cJSON_CreateNumber((double)minutes*60));
 /* recv_packet report format: con_clean=a[4], obstacles_cross=a[7]. */
 if((a[4]==0||a[4]==1)&&(a[7]==0||a[7]==1)){
  replace_value(status,"continue_clean",cJSON_CreateBool(a[4]));replace_value(status,"over_ridge",cJSON_CreateBool(a[7]));
 }
 unsigned char volume=255,volume_again=254;
 if(memory_read(fd,0x48f66,&volume,1)&&memory_read(fd,0x48f66,&volume_again,1)&&volume==volume_again&&volume<=100)replace_value(status,"volume",cJSON_CreateNumber(volume));
 replace_value(status,"statistics_units_source",cJSON_CreateString("vendor_history_contract"));
 replace_value(status,"telemetry_source",cJSON_CreateString("app_server_cache"));replace_value(status,"cache_read_at",cJSON_CreateNumber(time(NULL)));
 return true;
}
static int open_cache(void){
 if(!cache_pid)return -1;
 char path[128];
 if(!zigma_native_process(cache_pid,"/root/app_server/wifi_app_server_grit"))return -1;
 snprintf(path,sizeof(path),"/proc/%d/mem",cache_pid);return open(path,O_RDONLY);
}
static void enrich_status(cJSON *status){
 int fd=open_cache();if(fd<0)return;bool ok=cached_status(status,fd);close(fd);if(!ok)return;
 /* Queue activity is link evidence, never a timestamp for this status block. */
 key_t key=ftok("/",15);int q=key==(key_t)-1?-1:msgget(key,0);struct msqid_ds info;
 if(q>=0&&!msgctl(q,IPC_STAT,&info)&&info.msg_rtime>0){
  char path[128],line[128];snprintf(path,sizeof(path),"/proc/%d/status",info.msg_lrpid);FILE *f=fopen(path,"r");int tgid=0;
  if(f){while(fgets(line,sizeof(line),f))if(sscanf(line,"Tgid: %d",&tgid)==1)break;fclose(f);}
  if(tgid==cache_pid)replace_value(status,"local_link_received_at",cJSON_CreateNumber(info.msg_rtime));
 }
}
static bool cached_zones(cJSON *map,int fd){
 uint32_t pointer=0,count=0,again=0;if(!memory_read(fd,0x49d5c,&pointer,4)||!pointer||!memory_read(fd,pointer,&count,4)||count>64)return false;
 size_t size=4+(size_t)count*36;unsigned char a[2308],b[2308];if(!memory_read(fd,pointer,a,size)||!memory_read(fd,pointer,b,size)||memcmp(a,b,size)||memcmp(a,&count,4)||!memory_read(fd,0x49d5c,&again,4)||again!=pointer)return false;
 cJSON *origin=get(map,"map_origin"),*zones=cJSON_CreateArray();double ox=cJSON_GetArrayItem(origin,0)->valuedouble,oy=cJSON_GetArrayItem(origin,1)->valuedouble;
 for(uint32_t i=0;i<count;i++){uint32_t type;memcpy(&type,a+4+i*36,4);if(type>3){cJSON_Delete(zones);return false;}cJSON *zone=cJSON_CreateObject(),*points=cJSON_CreateArray();
  for(int j=0;j<4;j++){float x,y;memcpy(&x,a+8+i*36+j*8,4);memcpy(&y,a+12+i*36+j*8,4);if(!isfinite(x)||!isfinite(y)||fabs(x)>20000||fabs(y)>20000){cJSON_Delete(points);cJSON_Delete(zone);cJSON_Delete(zones);return false;}cJSON *point=cJSON_CreateObject();cJSON_AddNumberToObject(point,"x",x+ox);cJSON_AddNumberToObject(point,"y",-y+oy);cJSON_AddItemToArray(points,point);}
  cJSON_AddItemToObject(zone,"points",points);cJSON_AddBoolToObject(zone,"can_clean",type!=2&&type!=3);cJSON_AddBoolToObject(zone,"can_mop",type!=1&&type!=3);cJSON_AddNumberToObject(zone,"id",i+1);cJSON_AddStringToObject(zone,"source","vendor_cache");cJSON_AddItemToArray(zones,zone);
 }replace_value(map,"zones",zones);cJSON_AddBoolToObject(map,"zones_available",true);cJSON_AddNumberToObject(map,"zones_read_at",time(NULL));return true;
}
static bool enrich_zones(cJSON *map){
 if(!cache_pid){return false;}char file[128];if(!zigma_native_process(cache_pid,"/root/app_server/wifi_app_server_grit"))return false;
 snprintf(file,sizeof(file),"/proc/%d/mem",cache_pid);int fd=open(file,O_RDONLY);if(fd<0)return false;bool ok=cached_zones(map,fd);close(fd);return ok;
}
/* Configuration replies can replace the full map in SHM. Keep raster/path as
 * received and refresh only the independently observed zone cache. */
static char *published_zone_signature=NULL;
static char *zone_signature(cJSON *map){return cJSON_PrintUnformatted(get(map,"zones"));}
static cJSON *published_map(const char *dir){
 char file[1024];snprintf(file,sizeof(file),"%s/map.json",dir);FILE *f=fopen(file,"rb");if(!f)return NULL;
 struct stat st;if(fstat(fileno(f),&st)||st.st_size<=0||st.st_size>2*1024*1024){fclose(f);return NULL;}
 char *buf=malloc((size_t)st.st_size+1);if(!buf){fclose(f);return NULL;}size_t n=fread(buf,1,(size_t)st.st_size,f);fclose(f);buf[n]=0;cJSON *map=n==(size_t)st.st_size?cJSON_Parse(buf):NULL;free(buf);
 if(!cJSON_IsTrue(get(map,"available"))||!cJSON_IsArray(get(map,"cells"))||number(map,"width")<=0||number(map,"height")<=0||cJSON_GetArraySize(get(map,"map_origin"))!=2){cJSON_Delete(map);return NULL;}return map;
}
static bool merge_zone_cache(cJSON *map,cJSON *probe){
 const char *keys[]={"map_id","width","height","map_origin"};for(int i=0;i<4;i++)if(!cJSON_Compare(get(map,keys[i]),get(probe,keys[i]),1))return false;
 if(!cJSON_IsTrue(get(probe,"zones_available"))||!cJSON_IsArray(get(probe,"zones"))||cJSON_Compare(get(map,"zones"),get(probe,"zones"),1))return false;
 replace_value(map,"zones",cJSON_Duplicate(get(probe,"zones"),1));replace_value(map,"zones_available",cJSON_CreateBool(1));replace_value(map,"zones_read_at",cJSON_Duplicate(get(probe,"zones_read_at"),1));return true;
}
static void remember_published_map(cJSON *map){remember_map_geometry(map);free(published_zone_signature);published_zone_signature=zone_signature(map);}
static cJSON *refresh_published_zones(const char *dir){
 if(!map_geometry)return NULL;
 cJSON *probe=cJSON_Duplicate(map_geometry,1);if(!enrich_zones(probe)){cJSON_Delete(probe);return NULL;}
 char *signature=zone_signature(probe);cJSON *map=NULL;
 if(signature&&(!published_zone_signature||strcmp(signature,published_zone_signature))){map=published_map(dir);if(!map||!merge_zone_cache(map,probe)){cJSON_Delete(map);map=NULL;}else{free(published_zone_signature);published_zone_signature=signature;signature=NULL;}}
 free(signature);cJSON_Delete(probe);return map;
}

static bool request_map(void){key_t key=ftok("/",1);int q=key==(key_t)-1?-1:msgget(key,0);if(q<0)return false;struct{long type;char text[96];}message={2,"{\"ProtocolVersion\":\"1.0\",\"send_map_flag\":true}"};return msgsnd(q,&message,strlen(message.text)+1,IPC_NOWAIT)==0;}
static cJSON *spatial_document(cJSON *raw,bool geometry_only){
 int w=number(raw,"MapWidth"),h=number(raw,"MapHigh");cJSON *origin=get(raw,"MapOrigin");
 if(w<=0||h<=0||w>2048||h>2048||w*h>1000000||cJSON_GetArraySize(origin)!=2)return NULL;
 cJSON *ox=cJSON_GetArrayItem(origin,0),*oy=cJSON_GetArrayItem(origin,1);if(!cJSON_IsNumber(ox)||!cJSON_IsNumber(oy)||!isfinite(ox->valuedouble)||!isfinite(oy->valuedouble))return NULL;
 cJSON *o=cJSON_CreateObject();
 if(!geometry_only){size_t len;unsigned char *data=decode(get(raw,"MapData"),&len);if(!data||len!=((size_t)w*h+3)/4){free(data);cJSON_Delete(o);return NULL;}
 cJSON *cells=cJSON_CreateArray();for(int i=0;i<w*h;i++)cJSON_AddItemToArray(cells,cJSON_CreateNumber((data[i/4]>>(6-2*(i%4)))&3));free(data);cJSON_AddItemToObject(o,"cells",cells);}
 cJSON_AddItemToObject(o,"map_origin",cJSON_Duplicate(origin,1));cJSON_AddNumberToObject(o,"width",w);cJSON_AddNumberToObject(o,"height",h);cJSON_AddBoolToObject(o,"available",true);
 cJSON_AddStringToObject(o,"source","local_shm_snapshot");cJSON_AddStringToObject(o,"freshness","unknown");cJSON_AddNumberToObject(o,"snapshot_read_at",time(NULL));
 cJSON_AddStringToObject(o,"warning","Cached shared-memory map; freshness unknown. Existing no-go zones are not shown. Grid interpretation is provisional.");
 cJSON *id=get(raw,"map_id");if(cJSON_IsString(id))cJSON_AddStringToObject(o,"map_id",id->valuestring);
 cJSON_AddItemToObject(o,"zones",cJSON_CreateArray());cJSON *dock=get(raw,"ChargerPoint"),*dx=cJSON_GetArrayItem(dock,0),*dy=cJSON_GetArrayItem(dock,1);
 if(cJSON_IsNumber(dx)&&cJSON_IsNumber(dy))point(o,"dock",dx->valuedouble+ox->valuedouble,dy->valuedouble+oy->valuedouble);
 size_t bytes=0,types_len=0;unsigned char *pts=decode(get(raw,"PointData"),&bytes),*types=decode(get(raw,"PointType"),&types_len);cJSON *path=cJSON_CreateArray();
 if(pts&&types&&bytes%4==0&&types_len==(bytes/4+3)/4)for(size_t i=0;i<bytes;i+=4){
  if(geometry_only&&i/4+512<bytes/4)continue;
  int x=(short)((unsigned)pts[i]|((unsigned)pts[i+1]<<8)),y=(short)((unsigned)pts[i+2]|((unsigned)pts[i+3]<<8));
  if(bytes==4&&x==0&&y==0)continue;
  double px=x/10.0+ox->valuedouble,py=y/10.0+oy->valuedouble;if(px<0||px>=w||py<0||py>=h)continue;
  cJSON *p=cJSON_CreateObject();cJSON_AddNumberToObject(p,"x",px);cJSON_AddNumberToObject(p,"y",py);cJSON_AddItemToArray(path,p);
 }
 free(pts);free(types);if(cJSON_GetArraySize(path)){cJSON *last=cJSON_GetArrayItem(path,cJSON_GetArraySize(path)-1);cJSON *endpoint=cJSON_Duplicate(last,1);cJSON_AddStringToObject(endpoint,"kind","recorded_path_endpoint");cJSON_AddBoolToObject(endpoint,"live",false);cJSON_AddItemToObject(o,"path_endpoint",endpoint);}
 cJSON_AddItemToObject(o,"path",path);
 if(geometry_only)return o; /* Never merge unrelated native zones or renew path estimates. */
 char *signature=cJSON_PrintUnformatted(path);if(signature){if(last_path_signature&&strcmp(signature,last_path_signature))path_changed_at=time(NULL);free(last_path_signature);last_path_signature=signature;}
 cJSON_AddNumberToObject(o,"path_changed_at",path_changed_at);
 if(path_changed_at&&time(NULL)-path_changed_at<=6&&cJSON_GetArraySize(path)){cJSON *estimate=cJSON_Duplicate(cJSON_GetArrayItem(path,cJSON_GetArraySize(path)-1),1);cJSON_AddStringToObject(estimate,"kind","recent_path_estimate");cJSON_AddNumberToObject(estimate,"received_at",path_changed_at);cJSON_AddNullToObject(estimate,"heading");cJSON_AddItemToObject(o,"position_estimate",estimate);}
 if(enrich_zones(o)){cJSON_ReplaceItemInObjectCaseSensitive(o,"warning",cJSON_CreateString("No-go zones read from robot. Position is estimated from recent path updates; heading unavailable."));}
 /* A recent path endpoint is an estimate, never an independently measured pose. */
 return o;
}
static cJSON *map_document(cJSON *raw){return spatial_document(raw,false);}
/* Geometry-only reports may omit the raster frame. Keep native polygon units,
 * fit their own bounds, and never borrow the retained raster origin or pose. */
static cJSON *geometry_zones(cJSON *raw,cJSON *o){
 cJSON *zones=get(raw,"forbidden_zone_info"),*out=cJSON_CreateArray();
 double minx=20001,miny=20001,maxx=-20001,maxy=-20001;
 if(cJSON_IsArray(zones)&&cJSON_GetArraySize(zones)<=64)for(cJSON *z=zones->child;z;z=z->next){
  cJSON *xs=get(z,"x"),*ys=get(z,"y");bool valid=cJSON_GetArraySize(xs)==4&&cJSON_GetArraySize(ys)==4&&cJSON_IsBool(get(z,"can_clean"))&&cJSON_IsBool(get(z,"can_mop"));
  for(int i=0;i<4&&valid;i++){cJSON *x=cJSON_GetArrayItem(xs,i),*y=cJSON_GetArrayItem(ys,i);valid=cJSON_IsNumber(x)&&cJSON_IsNumber(y)&&isfinite(x->valuedouble)&&isfinite(y->valuedouble)&&fabs(x->valuedouble)<=20000&&fabs(y->valuedouble)<=20000;}
  if(!valid)continue;
  cJSON *zone=cJSON_CreateObject(),*points=cJSON_CreateArray();
  for(int i=0;i<4;i++){double x=cJSON_GetArrayItem(xs,i)->valuedouble,y=cJSON_GetArrayItem(ys,i)->valuedouble;minx=fmin(minx,x);miny=fmin(miny,y);maxx=fmax(maxx,x);maxy=fmax(maxy,y);cJSON *pt=cJSON_CreateObject();cJSON_AddNumberToObject(pt,"x",x);cJSON_AddNumberToObject(pt,"y",y);cJSON_AddItemToArray(points,pt);}
  cJSON_AddItemToObject(zone,"points",points);cJSON_AddBoolToObject(zone,"can_clean",cJSON_IsTrue(get(z,"can_clean")));cJSON_AddBoolToObject(zone,"can_mop",cJSON_IsTrue(get(z,"can_mop")));cJSON_AddItemToArray(out,zone);
 }
 if(!o&&cJSON_GetArraySize(out)){o=cJSON_CreateObject();double padding=5;minx=floor(minx-padding);miny=floor(miny-padding);cJSON_AddNumberToObject(o,"width",ceil(maxx-minx+padding));cJSON_AddNumberToObject(o,"height",ceil(maxy-miny+padding));cJSON_AddBoolToObject(o,"available",true);cJSON *origin=cJSON_CreateArray();cJSON_AddItemToArray(origin,cJSON_CreateNumber(-minx));cJSON_AddItemToArray(origin,cJSON_CreateNumber(-miny));cJSON_AddItemToObject(o,"map_origin",origin);cJSON_AddStringToObject(o,"frame","native_geometry_bounds");}
 if(o){cJSON *origin=get(o,"map_origin");double ox=cJSON_GetArrayItem(origin,0)->valuedouble,oy=cJSON_GetArrayItem(origin,1)->valuedouble;
  for(cJSON *z=out->child;z;z=z->next)for(cJSON *pt=get(z,"points")->child;pt;pt=pt->next){replace_value(pt,"x",cJSON_CreateNumber(get(pt,"x")->valuedouble+ox));replace_value(pt,"y",cJSON_CreateNumber(get(pt,"y")->valuedouble+oy));}
  replace_value(o,"zones",out);
 }else cJSON_Delete(out);
 return o;
}
static cJSON *read_core_geometry(const char *dir,const cJSON *health){
 cJSON *raw=core_load(dir,"spatial.json",262144),*o=NULL;
 if(raw&&core_generation(raw,health)&&!cJSON_IsTrue(get(raw,"CUploadRecord"))){
  o=geometry_zones(raw,spatial_document(raw,true));
  if(o){replace_value(o,"source",cJSON_CreateString("local_core_geometry"));replace_value(o,"warning",cJSON_CreateString("Partial received geometry; measurement time unknown. No floor raster."));replace_value(o,"received_at",cJSON_Duplicate(get(raw,"received_at"),1));replace_value(o,"cache_read_at",cJSON_CreateNumber(time(NULL)));}
 }
 cJSON_Delete(raw);return o;
}
static cJSON *changed_map_document(const char *text){
 cJSON *result=NULL;
   static char *previous_raw=NULL,*previous_zones=NULL;
   cJSON *raw=cJSON_Parse(text);if(raw){
    cJSON *probe=cJSON_CreateObject();cJSON *origin=get(raw,"MapOrigin");
    if(cJSON_GetArraySize(origin)==2&&cJSON_IsNumber(cJSON_GetArrayItem(origin,0))&&cJSON_IsNumber(cJSON_GetArrayItem(origin,1))){
     cJSON_AddItemToObject(probe,"map_origin",cJSON_Duplicate(origin,1));enrich_zones(probe);
    }
    cJSON_DeleteItemFromObjectCaseSensitive(probe,"zones_read_at");char *zones=cJSON_PrintUnformatted(probe);cJSON_Delete(probe);
    bool changed=!previous_raw||strcmp(previous_raw,text)||!previous_zones||!zones||strcmp(previous_zones,zones);
    if(changed){result=map_document(raw);if(result){free(previous_raw);previous_raw=strdup(text);free(previous_zones);previous_zones=zones;zones=NULL;}}
    free(zones);cJSON_Delete(raw);
   }
 return result;
}
static cJSON *read_map(void){
 key_t key=ftok("/",3);if(key==(key_t)-1)return NULL;int id=shmget(key,1,0);if(id<0)return NULL;
 struct shmid_ds info;if(shmctl(id,IPC_STAT,&info)||info.shm_segsz!=204800)return NULL;
 const void *mem=shmat(id,NULL,SHM_RDONLY);if(mem==(void*)-1)return NULL;
 char *a=malloc(info.shm_segsz+1),*b=malloc(info.shm_segsz+1);cJSON *result=NULL;
 if(a&&b){memcpy(a,mem,info.shm_segsz);memcpy(b,mem,info.shm_segsz);a[info.shm_segsz]=0;
  if(!memcmp(a,b,info.shm_segsz)&&memchr(a,0,info.shm_segsz)){
   result=changed_map_document(a);
  }}
 shmdt(mem);free(a);free(b);return result;
}
static cJSON *read_core_map(const char *dir,const cJSON *health){
 cJSON *raw=core_raster(dir,health);if(!raw)return NULL;
 cJSON *at=get(raw,"received_at");double received=cJSON_IsNumber(at)&&isfinite(at->valuedouble)&&at->valuedouble>0?at->valuedouble:0;
 const char *metadata[]={"received_at","received_monotonic_ms","receiver_pid","receiver_start_ticks","measurement_time",NULL};
 for(unsigned j=0;metadata[j];j++)cJSON_DeleteItemFromObjectCaseSensitive(raw,metadata[j]);
 char *text=cJSON_PrintUnformatted(raw);cJSON *map=NULL;if(text){map=changed_map_document(text);free(text);}
 if(map){
  replace_value(map,"source",cJSON_CreateString("local_core_ipc"));
  replace_value(map,"core_received_at",cJSON_CreateNumber(received));
  /* A publisher restart must not make a pre-clear/pre-restore raster look new. */
  replace_value(map,"snapshot_read_at",cJSON_CreateNumber(received));
 }
 cJSON_Delete(raw);return map;
}
#include "zigma_laser_observer.h"
int main(int argc,char **argv){
 if(zigma_version_only(argc,argv,"zigma-observe-arm"))return 0;
 if(argc<3){fprintf(stderr,"Usage: zigma-observe DATA_DIR APP_SERVER_LOG [--request-info] [--request-map] [--app-server-pid PID] [--nav-pid PID] [--core-dir PATH --core-exe PATH]\n");return 2;}
 const char *core_dir=NULL,*core_exe=NULL;
 bool info_requests=false;for(int i=3;i<argc;i++){if(!strcmp(argv[i],"--request-info"))info_requests=true;else if(!strcmp(argv[i],"--request-map"))map_requests=true;else if(!strcmp(argv[i],"--core-dir")&&i+1<argc)core_dir=argv[++i];else if(!strcmp(argv[i],"--core-exe")&&i+1<argc)core_exe=argv[++i];else if(!strcmp(argv[i],"--nav-pid")&&i+1<argc){nav_pid=atoi(argv[++i]);if(nav_pid<1)return 2;}else if(!strcmp(argv[i],"--app-server-pid")&&i+1<argc){cache_pid=atoi(argv[++i]);if(cache_pid<1)return 2;}else return 2;}
 if((core_dir&&!core_exe)||(!core_dir&&core_exe)||(core_dir&&(core_dir[0]!='/'||core_exe[0]!='/')))return 2;
 setenv("TZ","Europe/Madrid",1);tzset();umask(077);signal(SIGTERM,stop);signal(SIGINT,stop);
 cJSON *previous=published_map(argv[1]);if(previous){remember_published_map(previous);cJSON_Delete(previous);}
 long long next_status=0;time_t requested=0,map_requested=0;while(running){
  long long tick=core_clock_ms();bool laser_active=laser_tick(argv[1],tick);
  if(tick<next_status){long long delay=next_status-tick;if(laser_active&&laser_next_sample-tick<delay)delay=laser_next_sample-tick;if(delay<1)delay=1;if(delay>2500)delay=2500;struct timespec wait={delay/1000,(delay%1000)*1000000};nanosleep(&wait,NULL);continue;}
  next_status=tick+2500;
  char demand[1024];snprintf(demand,sizeof(demand),"%s/map-demand",argv[1]);struct stat demand_stat;if(map_requests&&!stat(demand,&demand_stat)&&time(NULL)-demand_stat.st_mtime>=0&&time(NULL)-demand_stat.st_mtime<10&&time(NULL)-map_requested>=2){if(!request_map())fprintf(stderr,"Local map request unavailable\n");map_requested=time(NULL);}
  char zonepath[256]={0};ssize_t zn=readlink("/root/etc/localtime",zonepath,sizeof(zonepath)-1);const char *prefix="/usr/share/zoneinfo/";
  if(zn>0&&!strncmp(zonepath,prefix,strlen(prefix))){const char *zone=zonepath+strlen(prefix);if(!getenv("TZ")||strcmp(getenv("TZ"),zone)){setenv("TZ",zone,1);tzset();}}if(info_requests&&time(NULL)-requested>=20){if(!request_info())fprintf(stderr,"Local info request unavailable\n");requested=time(NULL);}
  static cJSON *last_status=NULL;
  cJSON *status=read_status(argv[2]);
  if(number(status,"observed_at")>0){cJSON_Delete(last_status);last_status=cJSON_Duplicate(status,1);}
  else if(last_status){int finished=number(status,"clean_finished_at");cJSON_Delete(status);status=cJSON_Duplicate(last_status,1);if(finished)replace_value(status,"clean_finished_at",cJSON_CreateNumber(finished));}
  enrich_status(status);
  cJSON *core=core_dir?core_load(core_dir,"health.json",16384):NULL;
  bool core_active=core_identity(core,core_exe,core_clock_ms());
  if(core_dir){
   replace_value(status,"core_active",cJSON_CreateBool(core_active));
   if(core_active){cJSON *report=core_load(core_dir,"status.json",16384);core_merge_status(status,report,core);cJSON_Delete(report);
    cJSON *ack=core_load(core_dir,"last-response.json",8192);if(core_generation(ack,core))replace_value(status,"core_ack",cJSON_Duplicate(ack,1));cJSON_Delete(ack);
   }
  }
  if(core_active){cJSON *geometry=read_core_geometry(core_dir,core);if(geometry)replace_value(status,"map_geometry",geometry);}
  enrich_navigation_pose(status);
  cJSON_AddNumberToObject(status,"observer_seen_at",time(NULL));
  replace_value(status,"observer_monotonic_ms",cJSON_CreateNumber((double)core_clock_ms()));
  replace_value(status,"observer_pid",cJSON_CreateNumber(getpid()));
  replace_value(status,"observer_start_ticks",cJSON_CreateNumber((double)zigma_start_ticks(getpid())));
  if(!save(argv[1],"status.json",status)){cJSON_Delete(core);cJSON_Delete(status);perror("status write");return 1;}cJSON_Delete(status);
  cJSON *map=NULL;
  if(core_active)map=read_core_map(core_dir,core);
  else map=read_map();
  cJSON_Delete(core);
  if(!map)map=refresh_published_zones(argv[1]);
  if(map){remember_published_map(map);if(!save(argv[1],"map.json",map)){cJSON_Delete(map);perror("map write");return 1;}cJSON_Delete(map);}
 }laser_close();free(last_path_signature);free(published_zone_signature);cJSON_Delete(map_geometry);return 0;
}
