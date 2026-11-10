#include "zigma_pairing_page.h"
static bool http_reply(int fd,int code,const char *body){
 char head[1024],location[128]="";if(code==302)snprintf(location,sizeof location,"Location: http://%s:%u/\r\n",ap_ip,ap_port);
 int n=snprintf(head,sizeof head,"HTTP/1.1 %d %s\r\n%sContent-Type: text/html; charset=utf-8\r\nContent-Length: %zu\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nContent-Security-Policy: default-src 'none'; script-src 'nonce-%s'; style-src 'unsafe-inline'; connect-src 'self'; form-action 'self'; frame-ancestors 'none'\r\nConnection: close\r\n\r\n",code,code==200?"OK":code==302?"Found":"Bad Request",location,strlen(body),csrf);
 const char *parts[]={head,body};size_t lengths[]={(size_t)n,strlen(body)};
 for(unsigned i=0;i<2;i++){size_t off=0;while(off<lengths[i]){ssize_t sent=send(fd,parts[i]+off,lengths[i]-off,MSG_NOSIGNAL);if(sent<0&&errno==EINTR)continue;if(sent<=0)return false;off+=(size_t)sent;}}
 return true;
}
static void portal_error(int fd,const char *message){
 char path[1024],text[700];if(join(path,sizeof path,runtime,"pairing-last-error"))private_write(path,message);
 snprintf(text,sizeof text,"<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'><h1>Setup needs attention</h1><p>%s</p><p><a href='/'>Reload setup</a></p>",message);
 http_reply(fd,400,text);
}
static int portal_step(void){
 dns_step();scan_step();
 if(pending_action&&mono_ms()>=action_at){int action=pending_action;pending_action=0;return action;}
 int fd=accept(portal,NULL,NULL);if(fd<0)return 0;
 fcntl(fd,F_SETFD,FD_CLOEXEC);struct timeval tv={0,300000};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof tv);setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof tv);
 char req[32768]={0};size_t used=0;char *body=NULL;long long end=mono_ms()+2000;
 while(used<4096&&mono_ms()<end){ssize_t n=recv(fd,req+used,4096-used,0);if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)){dns_step();if(!used)break;continue;}if(n<=0)break;used+=(size_t)n;req[used]=0;body=strstr(req,"\r\n\r\n");if(body)break;}
 /* Browsers preconnect without sending a request. Never queue an HTTP error
  * on an idle connection: it can be mistaken for the next navigation. */
 if(!used){close(fd);return 0;}
 bool valid=body!=NULL;size_t content_length=0;bool length_seen=false,host_seen=false,canonical=false,type_ok=false;
 char method[12]="",path[1024]="",version[16]="";
 if(valid){
  body+=4;char *line_end=strstr(req,"\r\n");if(!line_end)valid=false;
  else{*line_end=0;char tail;if(sscanf(req,"%11s %1023s %15s %c",method,path,version,&tail)!=3||(strcmp(version,"HTTP/1.1")&&strcmp(version,"HTTP/1.0")))valid=false;
   char *p=line_end+2;
   while(valid&&p<body-2){char *e=strstr(p,"\r\n");if(!e){valid=false;break;}*e=0;char *colon=strchr(p,':');if(!colon){valid=false;break;}*colon++=0;while(*colon==' '||*colon=='\t')colon++;
    if(!strcasecmp(p,"Host")){char expected[80];snprintf(expected,sizeof expected,"%s:%u",ap_ip,ap_port);if(host_seen||!*colon)valid=false;canonical=!strcmp(colon,expected)||(ap_port==80&&!strcmp(colon,ap_ip));host_seen=true;}
    else if(!strcasecmp(p,"Content-Length")){char *q;unsigned long n=strtoul(colon,&q,10);if(length_seen||!isdigit((unsigned char)*colon)||*q||n>26000)valid=false;content_length=n;length_seen=true;}
    else if(!strcasecmp(p,"Transfer-Encoding"))valid=false;
    else if(!strcasecmp(p,"Content-Type")){const char *form="application/x-www-form-urlencoded";size_t n=strlen(form);type_ok=!strncasecmp(colon,form,n)&&(!colon[n]||colon[n]==';');}
    p=e+2;
   }
  }
 }
 valid=valid&&(host_seen||!strcmp(version,"HTTP/1.0"));
 if(valid&&!strcmp(method,"GET")&&!content_length){
  if(canonical&&!strcmp(path,"/networks")){
   char result[14500];snprintf(result,sizeof result,"<div id=scan-result data-busy='%d' data-failed='%d' data-count='%u'><select>%s</select></div>",scan_busy?1:0,scan_failed?1:0,scan_count,scan_options);http_reply(fd,200,result);
  }else if(!canonical||strcmp(path,"/"))http_reply(fd,302,"<p>Open the robot Wi-Fi setup page.</p>");
  else if(pending_action)http_reply(fd,200,"<h1>Connecting</h1><p>The setup network is closing. Reconnect your phone to your home Wi-Fi.</p>");
  else{
   char page[22000];snprintf(page,sizeof page,pairing_page,pair_error,csrf,scan_options,csrf,ap_name,
    ssh_listening()?"SSH is listening on port 22 and is available while connected to this setup network.":"SSH is not currently listening on the setup address. Adding a key does not start an SSH server.",ap_ip,csrf,csrf,pairing_scan_js);
   http_reply(fd,200,page);
  }
 }else if(valid&&canonical&&!strcmp(method,"POST")&&length_seen&&type_ok&&(!strcmp(path,"/connect")||!strcmp(path,"/cancel")||!strcmp(path,"/ssh")||!strcmp(path,"/scan"))){
  size_t offset=(size_t)(body-req);
  while(used-offset<content_length&&mono_ms()<end){ssize_t n=recv(fd,req+used,content_length-(used-offset),0);if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))continue;if(n<=0)break;used+=(size_t)n;}
  char ssid[33]="",password[64]="",token[65]="",key[8193]="";
  valid=used-offset==content_length&&memchr(body,0,content_length)==NULL;req[used]=0;
  bool already_pending=pending_action!=0;
  if(valid)valid=form_fields(body,ssid,password,token,key);
  if(!valid)portal_error(fd,"The form is incomplete or invalid. Reload setup and check the fields.");
  else if(strcmp(token,csrf))portal_error(fd,"This page belongs to an expired setup session. Reload setup before trying again.");
  else if(pending_action)portal_error(fd,"A network change is already in progress. Wait for the setup network to close.");
  else if(!strcmp(path,"/scan")){
   scan_start(0);http_reply(fd,200,"<p>Scanning networks. This may take up to 10 seconds.</p>");
  }else if(!strcmp(path,"/ssh")){
   if(ssh_upload(key))http_reply(fd,200,"<h1>SSH public key saved</h1><p>Existing keys were preserved.</p><p><a href='/'>Return to Wi-Fi setup</a></p>");
   else portal_error(fd,"Could not add the key. Use one valid OpenSSH public key and check available storage over SSH.");
  }else if(!strcmp(path,"/cancel")){
   if(http_reply(fd,200,"<h1>Setup cancelled</h1><p>Reconnecting to the previous Wi-Fi.</p>"))pending_action=2;
  }else if(candidate_write(ssid,password)){
   if(http_reply(fd,200,"<h1>Connecting</h1><p>The setup network will close in two seconds. Reconnect your phone to your home Wi-Fi. If connection fails, the robot setup network will return within a minute. Reopen http://192.168.99.1 to try again.</p>"))pending_action=1;
  }else portal_error(fd,"Could not prepare Wi-Fi settings. Check the network name (1-32 bytes), password (8-63 bytes), and available storage.");
  if(pending_action&&!already_pending)action_at=mono_ms()+2000;
  memset(password,0,sizeof password);memset(key,0,sizeof key);
 }else portal_error(fd,"Unsupported request. Open the setup page using the robot address and try again.");
 memset(req,0,sizeof req);close(fd);return 0;
}
