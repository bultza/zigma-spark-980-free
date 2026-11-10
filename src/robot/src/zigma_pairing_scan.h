/* Asynchronous native radio scan. The portal/DNS loop remains active. */
static const char *scan_bin="/usr/bin/iwlist";
static unsigned scan_timeout=10000;
static pid_t scan_pid;
static int scan_fd=-1;
static long long scan_deadline,scan_due;
static bool scan_busy,scan_failed,scan_overflow;
static char scan_output[131072],scan_options[14000];
static size_t scan_used;
static unsigned scan_count;
static void scan_stop(void){
 stop_child(&scan_pid);if(scan_fd>=0){close(scan_fd);scan_fd=-1;}scan_busy=false;
}
static void scan_parse(void){
 char names[64][33];unsigned count=0;size_t used=0;scan_options[0]=0;
 char *save=NULL;
 /* iwlist emits each SSID as the first 802.11 IE. Decode the raw hex IE,
  * avoiding ambiguous quotation/escaping in the human-readable ESSID line. */
 for(char *row=strtok_r(scan_output,"\n",&save);row&&count<64;row=strtok_r(NULL,"\n",&save)){
  char *p=strstr(row,"IE: Unknown: 00");if(!p)continue;p+=15;
  size_t hexlen=strcspn(p," \r\t");if(hexlen<2)continue;
  int a=unhex(p[0]),b=unhex(p[1]);if(a<0||b<0)continue;
  size_t n=(size_t)(a*16+b);if(!n||n>32||hexlen!=2+2*n)continue;
  char name[33];bool valid=true;
  for(size_t i=0;i<n;i++){a=unhex(p[2+2*i]);b=unhex(p[3+2*i]);if(a<0||b<0){valid=false;break;}unsigned c=(unsigned)(a*16+b);if(c<32||c==127){valid=false;break;}name[i]=(char)c;}
  name[n]=0;if(!valid)continue;
  bool duplicate=false;for(unsigned i=0;i<count;i++)if(!strcmp(names[i],name))duplicate=true;
  if(duplicate)continue;
  strcpy(names[count++],name);
  char escaped[200];size_t e=0;
  for(size_t i=0;i<n;i++){const char *entity=NULL;switch(name[i]){case '&':entity="&amp;";break;case '<':entity="&lt;";break;case '>':entity="&gt;";break;case '"':entity="&quot;";break;case '\'':entity="&#39;";break;}
   if(entity){size_t l=strlen(entity);memcpy(escaped+e,entity,l);e+=l;}else escaped[e++]=name[i];}
  escaped[e]=0;int added=snprintf(scan_options+used,sizeof scan_options-used,"<option>%s</option>",escaped);
  if(added<0||(size_t)added>=sizeof scan_options-used)break;
  used+=(size_t)added;
 }
 scan_count=count;
}
static void scan_start(unsigned delay){
 if(scan_busy)return;
 scan_busy=true;scan_failed=false;scan_overflow=false;scan_used=0;
 scan_due=mono_ms()+delay;scan_deadline=mono_ms()+scan_timeout;
}
static void scan_drain(void){
 char b[2048];ssize_t n;
 while((n=read(scan_fd,b,sizeof b))>0){
  if((size_t)n>=sizeof scan_output-scan_used)scan_overflow=true;
  else if(!scan_overflow){memcpy(scan_output+scan_used,b,(size_t)n);scan_used+=(size_t)n;}
 }
 scan_output[scan_used]=0;
}
static void scan_step(void){
 if(!scan_busy)return;
 if(mono_ms()>=scan_deadline){scan_stop();scan_failed=true;return;}
 if(!scan_pid){
  if(mono_ms()<scan_due)return;
  int fds[2];if(pipe(fds)){scan_busy=false;scan_failed=true;return;}
  fcntl(fds[0],F_SETFD,FD_CLOEXEC);fcntl(fds[1],F_SETFD,FD_CLOEXEC);
  fcntl(fds[0],F_SETFL,O_NONBLOCK);scan_fd=fds[0];
  char *args[]={(char*)scan_bin,(char*)iface,"scan",NULL};scan_pid=spawn(args,fds[1]);close(fds[1]);
  if(scan_pid<=0){scan_stop();scan_failed=true;}return;
 }
 scan_drain();int status=0;pid_t done=waitpid(scan_pid,&status,WNOHANG);
 if(done==0)return;
 if(done<0&&errno==EINTR)return;
 scan_drain();scan_pid=0;close(scan_fd);scan_fd=-1;scan_busy=false;
 if(done<0||!WIFEXITED(status)||WEXITSTATUS(status)!=0||scan_overflow){scan_failed=true;return;}
 scan_parse();
}
