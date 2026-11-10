/* Direct Telegram notifications and commands with one dedicated bot per robot.
 * HTTPS runs in a bounded child process; the web loop never waits for the network.
 * Credentials stay in 0600 data files and a private pipe, never argv or responses. */
#include "zigma_errors.h"
static const char *telegram_curl=NULL,*telegram_ca=NULL;
static cJSON *tg_config;
static char tg_ram[256],tg_code[33],tg_result[160]="Not configured";
static time_t tg_expires,tg_since,tg_next,tg_poll,tg_session,tg_last_finish;
static long long tg_offset;
static time_t tg_return_at;
static bool tg_session_reported,tg_session_stats,tg_wait_clean_exit;
static void tg_control_view(cJSON *o);
static void tg_control_close(void);
static pid_t tg_pid;
static int tg_kind,tg_error=-1,tg_attempts;
static char tg_error_name[65];
static bool tg_initialized;
static void tg_cancel_poll(void){if(tg_pid&&tg_kind==1){kill(tg_pid,SIGKILL);waitpid(tg_pid,NULL,0);tg_pid=0;}}
static double tg_area,tg_seconds;
static cJSON *tg_queue;
static void tg_events_reset(void);
static cJSON *tg_read(const char *file,size_t limit){char *s=read_file(file,limit);cJSON *o=s?strict_json(s):NULL;free(s);return o;}
static bool tg_store(cJSON *o){char p[1024];path(p,sizeof p,"telegram.json");char *s=cJSON_PrintUnformatted(o);bool ok=s&&atomic_write(p,s);free(s);return ok;}
#include "zigma_telegram_system.h"
#include "zigma_telegram_session.h"
static void tg_init(void){
 if(tg_config)return;
 char p[1024];path(p,sizeof p,"telegram.json");tg_config=tg_read(p,8192);
 if(!cJSON_IsObject(tg_config)){cJSON_Delete(tg_config);tg_config=cJSON_CreateObject();setbool(tg_config,"enabled",true);setbool(tg_config,"finished",true);setbool(tg_config,"errors",true);setbool(tg_config,"map",true);}
 for(const char **k=(const char*[]){"activity","battery","docking",NULL};*k;k++)if(!get(tg_config,*k))setbool(tg_config,*k,false);
 /* Legacy master switch is no longer user-configurable. */
 if(!cJSON_IsTrue(get(tg_config,"enabled"))){setbool(tg_config,"enabled",true);tg_store(tg_config);}
 if(!get(tg_config,"commands"))setbool(tg_config,"commands",false);
 if(!get(tg_config,"system"))setbool(tg_config,"system",true);
 tg_session_restore();tg_queue=cJSON_CreateArray();tg_since=time(NULL);snprintf(tg_result,sizeof tg_result,"%s",*str(tg_config,"chat_id","")?"Ready":"Not linked");tg_system_init();
}
static bool tg_token_valid(const char *s){
 const char *colon=strchr(s,':');if(!colon||colon-s<5||colon-s>20||strlen(colon+1)<20||strlen(colon+1)>100)return false;
 for(const char *p=s;p<colon;p++)if(!isdigit((unsigned char)*p))return false;
 for(const char *p=colon+1;*p;p++)if(!isalnum((unsigned char)*p)&&*p!='-'&&*p!='_')return false;
 return true;
}
static cJSON *tg_view(void){
 tg_init();cJSON *o=cJSON_CreateObject();
 for(const char **k=(const char*[]){"enabled","finished","errors","map","activity","battery","system","commands","docking",NULL};*k;k++)setbool(o,*k,cJSON_IsTrue(get(tg_config,*k)));
 setbool(o,"available",telegram_curl&&telegram_ca);setbool(o,"token_configured",*str(tg_config,"token",""));setbool(o,"linked",*str(tg_config,"chat_id",""));
 setstr(o,"chat",str(tg_config,"chat_label",""));setstr(o,"status",tg_result);setnum(o,"pending",cJSON_GetArraySize(tg_queue));setbool(o,"busy",tg_pid!=0&&tg_kind!=1);setbool(o,"polling",tg_pid!=0&&tg_kind==1);
 tg_control_view(o);
 if(tg_expires>time(NULL)){setstr(o,"pairing_code",tg_code);setnum(o,"pairing_expires_at",tg_expires);}return o;
}
static bool tg_random(char out[33]){unsigned char b[16];int fd=open("/dev/urandom",O_RDONLY);if(fd<0)return false;ssize_t n=read(fd,b,sizeof b);close(fd);if(n!=sizeof b)return false;for(int i=0;i<16;i++)snprintf(out+i*2,3,"%02x",b[i]);return true;}
static bool tg_temp(void){
 if(*tg_ram)return true;
 snprintf(tg_ram,sizeof tg_ram,"/tmp/zigma-telegram-XXXXXX");if(!mkdtemp(tg_ram)){*tg_ram=0;return false;}return true;
}
static void tg_file(char *p,size_t n,const char *name){snprintf(p,n,"%s/%s",tg_ram,name);}
static void tg_clear_queue(void){tg_events_reset();const cJSON *job;cJSON_ArrayForEach(job,tg_queue){const char *p=str((cJSON*)job,"photo","");if(*p)unlink(p);}cJSON_Delete(tg_queue);tg_queue=cJSON_CreateArray();tg_attempts=0;tg_next=0;tg_initialized=false;tg_session=0;tg_since=time(NULL);tg_area=tg_seconds=0;tg_return_at=0;tg_session_reported=tg_session_stats=tg_wait_clean_exit=false;tg_session_checkpoint();}
static bool tg_save(cJSON *in,char *err,size_t cap){
 tg_init();tg_cancel_poll();if(tg_pid){snprintf(err,cap,"Telegram request running; retry in a moment");return false;}
 for(const char **k=(const char*[]){"finished","errors","map",NULL};*k;k++)if(!cJSON_IsBool(get(in,*k))){snprintf(err,cap,"All notification options are required");return false;}
 for(const char **k=(const char*[]){"activity","battery","system","commands","docking",NULL};*k;k++)if(get(in,*k)&&!cJSON_IsBool(get(in,*k))){snprintf(err,cap,"Notification options must be boolean");return false;}
 const char *newtoken=str(in,"token","");if(*newtoken&&!tg_token_valid(newtoken)){snprintf(err,cap,"Invalid bot token");return false;}
 cJSON *next=cJSON_Duplicate(tg_config,1);bool changed=*newtoken&&strcmp(newtoken,str(tg_config,"token",""));
 if(changed){setstr(next,"token",newtoken);setstr(next,"chat_id","");setstr(next,"chat_label","");setbool(next,"commands",false);}
 for(const char **k=(const char*[]){"finished","errors","map",NULL};*k;k++)setbool(next,*k,cJSON_IsTrue(get(in,*k)));
 for(const char **k=(const char*[]){"activity","battery","system","commands","docking",NULL};*k;k++)if(get(in,*k))setbool(next,*k,cJSON_IsTrue(get(in,*k)));
 setbool(next,"enabled",true);
 if(changed)setbool(next,"commands",false);
 if(cJSON_IsTrue(get(next,"commands"))&&(!*str(next,"chat_id","")||!telegram_curl||!telegram_ca)){cJSON_Delete(next);snprintf(err,cap,"Link a chat before enabling commands");return false;}
 if(!tg_store(next)){cJSON_Delete(next);snprintf(err,cap,"Could not save Telegram settings");return false;}
 tg_control_close();cJSON_Delete(tg_config);tg_config=next;tg_clear_queue();tg_system_config_changed(changed);tg_expires=0;snprintf(tg_result,sizeof tg_result,"%s",changed?"Token saved; link a chat":"Saved");return true;
}
/* Small RGB PNG encoder, stored DEFLATE blocks: no extra runtime dependency. */
static unsigned long tg_crc(const unsigned char *p,size_t n){unsigned long c=0xffffffffUL;while(n--){c^=*p++;for(int b=0;b<8;b++)c=(c>>1)^((c&1)?0xedb88320UL:0);}return c^0xffffffffUL;}
static void tg_be(unsigned char *p,unsigned long n){p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n;}
static bool tg_chunk(FILE *f,const char *kind,const unsigned char *data,size_t n){unsigned char *b=malloc(n+12);if(!b)return false;tg_be(b,n);memcpy(b+4,kind,4);if(n)memcpy(b+8,data,n);tg_be(b+8+n,tg_crc(b+4,n+4));bool ok=fwrite(b,1,n+12,f)==n+12;free(b);return ok;}
static bool tg_error_pose(cJSON *m,cJSON *pose,time_t now){
 if(!pose||strcmp(str(pose,"kind",""),"navigation_pose_cache")||strcmp(str(pose,"frame",""),"map_grid"))return false;
 const char *id=str(m,"map_id","");
 if(!*id||strcmp(id,str(pose,"map_id",""))||num(m,"width",0)!=num(pose,"width",-1)||num(m,"height",0)!=num(pose,"height",-1))return false;
 cJSON *a=get(m,"map_origin"),*b=get(pose,"map_origin");
 if(!cJSON_IsArray(a)||!cJSON_IsArray(b)||cJSON_GetArraySize(a)!=2||cJSON_GetArraySize(b)!=2)return false;
 for(int i=0;i<2;i++){cJSON *x=cJSON_GetArrayItem(a,i),*y=cJSON_GetArrayItem(b,i);if(!cJSON_IsNumber(x)||!cJSON_IsNumber(y)||!isfinite(x->valuedouble)||x->valuedouble!=y->valuedouble)return false;}
 for(const char **k=(const char*[]){"x","y","heading","cache_read_at",NULL};*k;k++){cJSON *v=get(pose,*k);if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble))return false;}
 double x=get(pose,"x")->valuedouble,y=get(pose,"y")->valuedouble,age=now-get(pose,"cache_read_at")->valuedouble;
 return age>=0&&age<=15&&x>=0&&y>=0&&x<num(m,"width",0)&&y<num(m,"height",0);
}
static bool tg_map_render(const char *dest,time_t since,cJSON *pose,time_t now){
 if(!map_file||(!since&&!pose)||reset_waiting())return false;
 if(pose){cJSON *transition=get(state,"map_reset_pending");if(now-num(transition,"completed_at",0)<15)return false;}
 cJSON *m=tg_read(map_file,2*1024*1024);int w=num(m,"width",0),h=num(m,"height",0);cJSON *cells=get(m,"cells");
 if(!m||w<1||h<1||w>2048||h>2048||w*h>262144||cJSON_GetArraySize(cells)!=w*h||(since&&num(m,"path_changed_at",0)<since)||(pose&&!tg_error_pose(m,pose,now))){cJSON_Delete(m);return false;}
 int outw=w>600?600:w,outh=h>600?600:h;size_t stride=(size_t)outw*3+1,n=stride*outh;unsigned char *raw=calloc(n,1),*grid=malloc((size_t)w*h);
 if(!raw||!grid){free(raw);free(grid);cJSON_Delete(m);return false;}
 int i=0;const cJSON *cell;bool valid=true;cJSON_ArrayForEach(cell,cells){if(!cJSON_IsNumber(cell)||cell->valueint<0||cell->valueint>3)valid=false;grid[i++]=(unsigned char)cell->valueint;}
 const unsigned char palette[4][3]={{27,34,45},{234,238,243},{117,130,146},{54,66,82}};
 for(int y=0;y<outh;y++)for(int x=0;x<outw;x++){int v=grid[(y*h/outh)*w+x*w/outw];if(v>3)v=0;memcpy(raw+y*stride+1+x*3,palette[v],3);}
 free(grid);
 /* Row zero remains at the top. Draw the recorded route in blue. */
 int px=-1,py=-1;const cJSON *pt;cJSON_ArrayForEach(pt,get(m,"path")){
  cJSON *xx=get((cJSON*)pt,"x"),*yy=get((cJSON*)pt,"y");if(!cJSON_IsNumber(xx)||!cJSON_IsNumber(yy)||!isfinite(xx->valuedouble)||!isfinite(yy->valuedouble)||xx->valuedouble<0||xx->valuedouble>=w||yy->valuedouble<0||yy->valuedouble>=h){px=py=-1;continue;}
  int x=xx->valuedouble*outw/w,y=yy->valuedouble*outh/h;if(px<0){px=x;py=y;}int steps=abs(x-px)>abs(y-py)?abs(x-px):abs(y-py);if(!steps)steps=1;
  for(int j=0;j<=steps;j++){int a=px+(x-px)*j/steps,b=py+(y-py)*j/steps;unsigned char *p=raw+b*stride+1+a*3;p[0]=49;p[1]=153;p[2]=255;}px=x;py=y;
 }
 /* Dock coordinates already belong to this raster. The green square is 8 by 8
  * pixels; the robot disc remains 11 pixels including its white rim. */
 cJSON *dock=get(m,"dock"),*dx=get(dock,"x"),*dy=get(dock,"y");
 if(cJSON_IsNumber(dx)&&cJSON_IsNumber(dy)&&isfinite(dx->valuedouble)&&isfinite(dy->valuedouble)&&dx->valuedouble>=0&&dy->valuedouble>=0&&dx->valuedouble<w&&dy->valuedouble<h){
  int cx=dx->valuedouble*outw/w,cy=dy->valuedouble*outh/h;
  for(int y=cy-4;y<cy+4;y++)for(int x=cx-4;x<cx+4;x++){
   if(x<0||y<0||x>=outw||y>=outh)continue;
   unsigned char *p=raw+y*stride+1+x*3;p[0]=34;p[1]=197;p[2]=94;
  }
 }
 /* A red disc with a white rim marks the navigation cache, never path end. */
 if(pose){
  int cx=get(pose,"x")->valuedouble*outw/w,cy=get(pose,"y")->valuedouble*outh/h;
  for(int dy=-5;dy<=5;dy++)for(int dx=-5;dx<=5;dx++){
   int x=cx+dx,y=cy+dy,d=dx*dx+dy*dy;if(d>25||x<0||y<0||x>=outw||y>=outh)continue;
   unsigned char *p=raw+y*stride+1+x*3;p[0]=255;p[1]=d>9?255:65;p[2]=d>9?255:65;
  }
 }
 cJSON_Delete(m);if(!valid){free(raw);return false;}
 /* Nearest-neighbor 2x scales the entire map and existing markers exactly. */
 int oldw=outw,oldh=outh;size_t oldstride=stride;outw*=2;outh*=2;
 stride=(size_t)outw*3+1;n=stride*outh;unsigned char *scaled=calloc(n,1);
 if(!scaled){free(raw);return false;}
 for(int y=0;y<oldh*2;y++)for(int x=0;x<oldw*2;x++)memcpy(scaled+y*stride+1+x*3,raw+(y/2)*oldstride+1+(x/2)*3,3);
 free(raw);raw=scaled;
 /* Heading is already in screen radians (+X right, +Y down). Draw after
  * scaling so the red orientation ray stays two output pixels wide. */
 if(pose){
  int x=(int)(get(pose,"x")->valuedouble*oldw/w)*2+1,y=(int)(get(pose,"y")->valuedouble*oldh/h)*2+1;
  double heading=get(pose,"heading")->valuedouble;
  int tx=x+(int)lround(18*cos(heading)),ty=y+(int)lround(18*sin(heading));
  int dx=abs(tx-x),dy=-abs(ty-y),sx=x<tx?1:-1,sy=y<ty?1:-1,err=dx+dy;
  for(;;){
   for(int offset=0;offset<2;offset++){
    int a=x+(dx < -dy?offset:0),b=y+(dx >= -dy?offset:0);
    if(a>=0&&b>=0&&a<outw&&b<outh){unsigned char *p=raw+b*stride+1+a*3;p[0]=255;p[1]=65;p[2]=65;}
   }
   if(x==tx&&y==ty)break;
   int twice=2*err;if(twice>=dy){err+=dy;x+=sx;}if(twice<=dx){err+=dx;y+=sy;}
  }
 }
 size_t zcap=n+6+5*(n/65535+1);unsigned char *z=malloc(zcap);if(!z){free(raw);return false;}size_t off=0,at=2;z[0]=0x78;z[1]=1;unsigned long a=1,b=0;
 for(size_t j=0;j<n;j++){a=(a+raw[j])%65521;b=(b+a)%65521;}
 while(off<n){size_t count=n-off;if(count>65535)count=65535;z[at++]=off+count==n?1:0;z[at++]=count;z[at++]=count>>8;z[at++]=~count;z[at++]=(~count)>>8;memcpy(z+at,raw+off,count);at+=count;off+=count;}tg_be(z+at,(b<<16)|a);at+=4;free(raw);
 FILE *f=fopen(dest,"wb");if(!f){free(z);return false;}unsigned char hdr[13]={0};tg_be(hdr,outw);tg_be(hdr+4,outh);hdr[8]=8;hdr[9]=2;
 bool ok=fwrite("\211PNG\r\n\032\n",1,8,f)==8&&tg_chunk(f,"IHDR",hdr,13)&&tg_chunk(f,"IDAT",z,at)&&tg_chunk(f,"IEND",NULL,0);if(fclose(f))ok=false;free(z);if(!ok)unlink(dest);return ok;
}
static bool tg_map_png(const char *dest,time_t since){return tg_map_render(dest,since,NULL,time(NULL));}
static bool tg_enqueue(const char *message,time_t session){
 if(cJSON_GetArraySize(tg_queue)>=16){snprintf(tg_result,sizeof tg_result,"Queue full; notification dropped");return false;}
 cJSON *o=cJSON_CreateObject();setstr(o,"text",message);setnum(o,"created",time(NULL));
 /* Snapshot now: later cleans must never replace an earlier notification's map. */
 char id[33],p[512];if(session&&cJSON_IsTrue(get(tg_config,"map"))&&tg_temp()&&tg_random(id)){char name[48];snprintf(name,sizeof name,"%s.png",id);tg_file(p,sizeof p,name);if(tg_map_png(p,session)){setstr(o,"photo",p);}}
 cJSON_AddItemToArray(tg_queue,o);return true;
}
static bool tg_error_enqueue(const char *message,cJSON *status,time_t now){
 if(!tg_enqueue(message,0))return false;
 if(!cJSON_IsTrue(get(tg_config,"map")))return true;
 cJSON *job=cJSON_GetArrayItem(tg_queue,cJSON_GetArraySize(tg_queue)-1);
 char id[33],p[512],caption[1024];bool attached=false;
 if(tg_temp()&&tg_random(id)){char name[48];snprintf(name,sizeof name,"%s.png",id);tg_file(p,sizeof p,name);
  if(tg_map_render(p,0,get(status,"navigation_pose"),now)){setstr(job,"photo",p);attached=true;}
 }
 snprintf(caption,sizeof caption,"%s\n%s",message,attached?"Map: cached position.":"Robot location unavailable.");
 setstr(job,"text",caption);return true;
}
#include "zigma_telegram_report.h"
#include "zigma_telegram_control.h"
static void tg_drop(void){cJSON *o=cJSON_DetachItemFromArray(tg_queue,0);const char *p=str(o,"photo","");if(*p)unlink(p);cJSON_Delete(o);tg_attempts=0;tg_next=time(NULL)+2;}
static bool tg_start(int kind){
 if(!telegram_curl||!telegram_ca||!tg_token_valid(str(tg_config,"token",""))||!tg_temp()||tg_pid)return false;
 char out[512],bodypath[512],config[4096];tg_file(out,sizeof out,"response.json");tg_file(bodypath,sizeof bodypath,"body.json");unlink(out);
 cJSON *body=cJSON_CreateObject(),*job=cJSON_GetArrayItem(tg_queue,0);const char *method="getUpdates",*photo="";
 if(kind==3){method="getMe";}
 else if(kind==1){setnum(body,"offset",(double)tg_offset);setnum(body,"timeout",tg_expires>time(NULL)?0:15);setnum(body,"limit",100);cJSON_AddItemToObject(body,"allowed_updates",cJSON_Parse("[\"message\"]"));}
 else{photo=str(job,"photo","");method=*photo?"sendPhoto":"sendMessage";setstr(body,"chat_id",str(tg_config,"chat_id",""));setstr(body,"text",str(job,"text",""));}
 char *json=cJSON_PrintUnformatted(body);cJSON_Delete(body);bool saved=json&&atomic_write(bodypath,json);free(json);if(!saved)return false;
 /* All variable paths are generated by us; token has a restricted alphabet. */
 int len=snprintf(config,sizeof config,"url = \"https://api.telegram.org/bot%s/%s\"\ncacert = \"%s\"\noutput = \"%s\"\nsilent\nconnect-timeout = 10\nmax-time = 25\nmax-filesize = 1048576\nproto = \"=https\"\nproxy = \"\"\n",str(tg_config,"token",""),method,telegram_ca,out);
 if(*photo){char caption[512];tg_file(caption,sizeof caption,"caption.txt");if(!atomic_write(caption,str(job,"text","")))return false;len+=snprintf(config+len,sizeof(config)-len,"form-string = \"chat_id=%s\"\nform = \"caption=<%s\"\nform = \"photo=@%s;type=image/png\"\n",str(tg_config,"chat_id",""),caption,photo);}
 else len+=snprintf(config+len,sizeof(config)-len,"header = \"Content-Type: application/json\"\ndata-binary = \"@%s\"\n",bodypath);
 if(len<0||len>=(int)sizeof config)return false;
 int pipes[2];if(pipe(pipes))return false;pid_t parent=getpid();pid_t child=fork();if(child<0){close(pipes[0]);close(pipes[1]);return false;}
 if(!child){if(prctl(PR_SET_PDEATHSIG,SIGKILL)||getppid()!=parent)_exit(126);dup2(pipes[0],STDIN_FILENO);close(pipes[0]);close(pipes[1]);int null=open("/dev/null",O_WRONLY);if(null>=0){dup2(null,STDOUT_FILENO);dup2(null,STDERR_FILENO);if(null>2)close(null);}if(listener>=0)close(listener);execl(telegram_curl,telegram_curl,"-q","--config","-",(char*)NULL);_exit(127);}
 close(pipes[0]);size_t at=0;while(at<(size_t)len){ssize_t n=write(pipes[1],config+at,len-at);if(n<=0)break;at+=n;}close(pipes[1]);tg_pid=child;tg_kind=kind;tg_poll=time(NULL);return true;
}
static void tg_finish_job(int rc){
 char p[512];tg_file(p,sizeof p,"response.json");cJSON *o=tg_read(p,1048576);unlink(p);tg_file(p,sizeof p,"body.json");unlink(p);tg_file(p,sizeof p,"caption.txt");unlink(p);
 bool ok=rc==0&&cJSON_IsTrue(get(o,"ok"));int code=num(o,"error_code",0);
 if(tg_kind==3){
  tg_system_result(ok,tg_network_failure(rc),tg_monotonic());
  if(code==429){int delay=num(get(o,"parameters"),"retry_after",120);if(delay<120)delay=120;if(delay>86400)delay=86400;tg_probe_next=tg_monotonic()+delay;}
  if(code==401)snprintf(tg_result,sizeof tg_result,"Invalid bot token");
 }else if(tg_kind==1){
  tg_system_result(ok,tg_network_failure(rc),tg_monotonic());
  if(ok){tg_control_result(o);}
  else{snprintf(tg_result,sizeof tg_result,"%s",code==409?"Bot token used elsewhere or webhook active; use one bot per robot":code==401?"Invalid bot token":"Command polling failed; check network and clock");
   int delay=code==429?num(get(o,"parameters"),"retry_after",30):5;if(delay<3)delay=3;if(delay>3600)delay=3600;tg_poll=time(NULL)+delay-3;
  }
 }else{
  if(ok){snprintf(tg_result,sizeof tg_result,"Last message sent");tg_drop();tg_system_result(true,false,tg_monotonic());}
  else if((rc==5||rc==6||rc==7||rc==35)&&tg_attempts<3){tg_next=time(NULL)+(tg_attempts==0?5:tg_attempts==1?30:120);tg_attempts++;snprintf(tg_result,sizeof tg_result,"Connection failed; retry scheduled");}
  else if(code==429&&tg_attempts<4){int delay=num(get(o,"parameters"),"retry_after",30);if(delay<1)delay=1;if(delay>3600)delay=3600;tg_next=time(NULL)+delay;tg_attempts++;snprintf(tg_result,sizeof tg_result,"Rate limited; retry scheduled");}
  else{snprintf(tg_result,sizeof tg_result,"%s",rc?"Delivery unconfirmed; check network and clock (not retried)":code==403?"Bot blocked or removed from chat":code==401?"Invalid bot token":"Telegram rejected the message");tg_drop();}
  if(!ok)tg_system_result(false,tg_network_failure(rc),tg_monotonic());
 }
 cJSON_Delete(o);
}
#include "zigma_telegram_events.h"
/* A return report is a snapshot, not evidence of successful completion. */
static bool tg_clean_report(cJSON *s,bool partial){
 if(!cJSON_IsTrue(get(tg_config,"finished"))&&!tg_control_ready())return true;
 bool stats=tg_session_stats||(cJSON_IsNumber(get(s,"clean_area_m2"))&&cJSON_IsNumber(get(s,"clean_time_s")));
 double area=tg_session?tg_area:(cJSON_IsNumber(get(s,"clean_area_m2"))?get(s,"clean_area_m2")->valuedouble:0),seconds=tg_session?tg_seconds:num(s,"clean_time_s",0);
 bool feet=!strcmp(str(state,"area_unit","m2"),"ft2");char message[900];
 const char *title=partial?"Partial cleaning report · Returning to dock / charging; completion not confirmed":"Cleaning finished";
 if(stats)snprintf(message,sizeof message,"%s\n%.1f %s · %.0f min · Battery %d%%",title,area*(feet?10.7639104:1),feet?"ft²":"m²",seconds/60,num(s,"battery_level",0));
 else snprintf(message,sizeof message,"%s\nStatistics unavailable",title);
 if(tg_enqueue(message,tg_session)){
  int last=cJSON_GetArraySize(tg_queue)-1;cJSON *job=cJSON_GetArrayItem(tg_queue,last);setnum(job,"session_id",tg_session);setbool(job,"partial_report",partial);setnum(job,"finish_at",partial?0:num(s,"clean_finished_at",0));
  if(!tg_report_store(job)){job=cJSON_DetachItemFromArray(tg_queue,last);const char *photo=str(job,"photo","");if(*photo)unlink(photo);cJSON_Delete(job);snprintf(tg_result,sizeof tg_result,"Could not save cleaning report; will retry");return false;}
  if(!cJSON_IsTrue(get(tg_config,"enabled"))||!cJSON_IsTrue(get(tg_config,"finished"))){job=cJSON_DetachItemFromArray(tg_queue,last);const char *photo=str(job,"photo","");if(*photo)unlink(photo);cJSON_Delete(job);}
  return true;
 }
 return false;
}
static void tg_observe(cJSON *s,time_t now){
 /* A recent cache read alone is not evidence of a live vendor link. */
 double ts=cJSON_IsNumber(get(s,"observed_at"))?get(s,"observed_at")->valuedouble:0;
 bool ready=(now-ts>=0&&now-ts<=30)||(now-num(s,"cache_read_at",0)>=0&&now-num(s,"cache_read_at",0)<=15&&now-num(s,"local_link_received_at",0)>=0&&now-num(s,"local_link_received_at",0)<=15);
 if(!ready){tg_events_initialized=false;tg_return_at=0;return;}
 int error=num(s,"vendor_error_code",-1),finish=num(s,"clean_finished_at",0);const char *work=str(s,"working_status","");
 bool clean=!strcmp(work,"AutoClean")||!strcmp(work,"SpotClean")||!strcmp(work,"SelectClean")||!strcmp(work,"PlanningRect");
 if(!tg_initialized){tg_initialized=true;tg_error=error;snprintf(tg_error_name,sizeof tg_error_name,"%s",str(s,"error","unknown"));if(finish>tg_last_finish)tg_last_finish=finish;if(cJSON_IsTrue(get(tg_config,"enabled")))tg_events_observe(s,now,true);tg_session_checkpoint();return;}
 if(reset_waiting()){tg_session=0;tg_return_at=0;tg_session_reported=true;tg_session_stats=false;tg_area=tg_seconds=0;}
 if(!clean)tg_wait_clean_exit=false;
 if(clean&&!tg_session&&!tg_wait_clean_exit&&!reset_waiting()){tg_session=now;tg_area=tg_seconds=0;tg_session_reported=tg_session_stats=false;}
 if(tg_session){if(cJSON_IsNumber(get(s,"clean_area_m2"))&&cJSON_IsNumber(get(s,"clean_time_s")))tg_session_stats=true;double area=cJSON_IsNumber(get(s,"clean_area_m2"))?get(s,"clean_area_m2")->valuedouble:0;double seconds=num(s,"clean_time_s",0);if(clean&&(area<tg_area||seconds<tg_seconds)){tg_area=area;tg_seconds=seconds;}else{if(area>tg_area)tg_area=area;if(seconds>tg_seconds)tg_seconds=seconds;}}
 char message[900];
 const char *error_name=str(s,"error","unknown");bool symbolic=strcmp(error_name,"unknown")&&strcmp(error_name,"NoError");
 bool error_changed=error>0?error!=tg_error:error<0&&symbolic&&strcmp(error_name,tg_error_name);
 if(cJSON_IsTrue(get(tg_config,"enabled"))&&error_changed&&cJSON_IsTrue(get(tg_config,"errors"))){
  char detail[512];zigma_error_text(detail,sizeof detail,error,error_name);
  snprintf(message,sizeof message,"Robot error\n%s",detail);
  tg_error_announced=tg_error_enqueue(message,s,now);
 }
 bool error_clear=error==0||(error<0&&!strcmp(error_name,"NoError"));
 if(cJSON_IsTrue(get(tg_config,"enabled"))&&error_clear&&(tg_error>0||(strcmp(tg_error_name,"unknown")&&strcmp(tg_error_name,"NoError")))&&tg_error_announced&&cJSON_IsTrue(get(tg_config,"errors"))){tg_notice("Robot error cleared");tg_error_announced=false;}
 if(error_clear)snprintf(tg_error_name,sizeof tg_error_name,"NoError");else if(strcmp(error_name,"unknown"))snprintf(tg_error_name,sizeof tg_error_name,"%s",error_name);
 if(error>=0)tg_error=error;
 if(finish>tg_last_finish&&finish>=tg_since&&finish<=now&&!reset_waiting()){
  if(!tg_finish_already_reported(s,finish)&&!tg_clean_report(s,false)){tg_session_checkpoint();return;}
  tg_wait_clean_exit=clean;tg_session_reported=true;tg_last_finish=finish;tg_session=0;tg_area=tg_seconds=0;tg_return_at=0;tg_session_stats=false;
 }
 bool returning=!strcmp(work,"BackCharging")||!strcmp(work,"PileCharging")||!strcmp(work,"DirCharging");
 /* Debounce the observed return, across command origins. Keep paused sessions
  * until a return; never manufacture a report from a dock command alone. */
 if(tg_session&&!tg_session_reported&&returning&&!reset_waiting()){
  if(!tg_return_at)tg_return_at=now;
  if(now-tg_return_at>=5&&tg_clean_report(s,true)){tg_session_reported=true;tg_session=0;tg_return_at=0;tg_area=tg_seconds=0;tg_session_stats=false;}
 }else tg_return_at=0;
 if(cJSON_IsTrue(get(tg_config,"enabled")))tg_events_observe(s,now,false);
 tg_session_checkpoint();
 /* Standby may be a pause: retain the session for a subsequent dock report. */
}
static void tg_tick(void){
 tg_init();time_t now=time(NULL);tg_control_tick();
 if(tg_pid){int status=0;pid_t done=waitpid(tg_pid,&status,WNOHANG);if(!done&&now-tg_poll>30){kill(tg_pid,SIGKILL);done=waitpid(tg_pid,&status,0);}if(done==tg_pid){tg_pid=0;tg_finish_job(WIFEXITED(status)?WEXITSTATUS(status):1);}}
 if(!telegram_curl||!telegram_ca)return;
 static time_t observed;if((cJSON_IsTrue(get(tg_config,"enabled"))||tg_control_ready())&&*str(tg_config,"chat_id","")&&now-observed>=5){observed=now;cJSON *s=status_file?tg_read(status_file,65536):NULL;tg_command_feedback(s,now);tg_observe(s,now);cJSON_Delete(s);}
 if(tg_pid)return;
 if(tg_expires&&now>=tg_expires){tg_expires=0;snprintf(tg_result,sizeof tg_result,"Pairing expired; try again");}
 if(!cJSON_GetArraySize(tg_queue)&&tg_control_poll()&&now-tg_poll>=3){tg_offset=tg_confirmed;tg_start(1);return;}
 if(!cJSON_GetArraySize(tg_queue)&&!tg_expires&&tg_system_state&&tg_system_enabled()&&tg_monotonic()>=tg_probe_next){if(!tg_start(3))tg_probe_next=tg_monotonic()+120;return;}
 if(cJSON_GetArraySize(tg_queue)&&now>=tg_next){cJSON *job=cJSON_GetArrayItem(tg_queue,0);if(now-num(job,"created",0)>3600){tg_drop();snprintf(tg_result,sizeof tg_result,"Notification expired");}else if(!tg_start(2)){tg_next=now+30;snprintf(tg_result,sizeof tg_result,"Could not start Telegram sender");}}
}
static void tg_stop(void){tg_song_stop();tg_session_checkpoint();free(tg_session_saved);tg_session_saved=NULL;tg_control_close();if(tg_pid){kill(tg_pid,SIGKILL);waitpid(tg_pid,NULL,0);tg_pid=0;}while(cJSON_GetArraySize(tg_queue))tg_drop();if(*tg_ram){char p[512];for(const char **n=(const char*[]){"body.json","response.json","caption.txt",NULL};*n;n++){tg_file(p,sizeof p,*n);unlink(p);}rmdir(tg_ram);}}
static void tg_api(int fd,const char *method,const char *url,cJSON *input){
 tg_init();if(!strcmp(method,"POST"))tg_cancel_poll();if(!strcmp(method,"PUT")&&!strcmp(url,"/api/telegram")){char err[160];if(!tg_save(input,err,sizeof err)){error_response(fd,400,err);return;}}
 else if(!strcmp(method,"POST")&&!strcmp(url,"/api/telegram/link")){
  if(tg_pid||!telegram_curl||!telegram_ca||!tg_token_valid(str(tg_config,"token",""))){error_response(fd,409,"Save a valid token first, or wait for the current request");return;}
  if(!tg_random(tg_code)){error_response(fd,500,"Could not create pairing code");return;}tg_expires=time(NULL)+300;tg_offset=0;tg_poll=0;snprintf(tg_result,sizeof tg_result,"Send the linking command to your bot");
 }else if(!strcmp(method,"POST")&&!strcmp(url,"/api/telegram/test")){
  if(!telegram_curl||!telegram_ca||!*str(tg_config,"chat_id","")||tg_expires){error_response(fd,409,"Link a chat first");return;}
  if(tg_pid||cJSON_GetArraySize(tg_queue)){error_response(fd,409,"A message is already pending");return;}tg_enqueue("Telegram test",0);snprintf(tg_result,sizeof tg_result,"Test queued");
 }else{error_response(fd,404,"Unsupported Telegram operation");return;}
 json_response(fd,200,tg_view());
}
