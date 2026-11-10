/* Foreground WPA/DHCP and physically requested SoftAP owner. No WAN probe. */
#define _POSIX_C_SOURCE 200809L
#include "zigma_version.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stopping;
static const char *iface="wlan0", *config="/root/yg/etc/wpa_supplicant.conf";
static const char *runtime="/tmp/zigma-network", *procroot="/proc";
static const char *wpa="/root/bin/wpa_supplicant", *cli="/root/bin/wpa_cli";
static const char *dhcp="/sbin/udhcpc", *ifconfig_bin="/sbin/ifconfig";
static const char *hook="/root/etc/udhcpc.script";
static unsigned poll_ms=5000, retry_ms=1000;
static pid_t wpa_pid, dhcp_pid;
static int lock_fd=-1;
static char state_path[1024], dhcp_path[1024];

static unsigned long long start_ticks(pid_t pid){
 if(pid<=0)return 0;
 char path[64],line[4096];snprintf(path,sizeof path,"/proc/%ld/stat",(long)pid);
 FILE *f=fopen(path,"r");if(!f)return 0;
 char *ok=fgets(line,sizeof line,f);fclose(f);if(!ok)return 0;
 char *p=strrchr(line,')');if(!p)return 0;p+=2;
 for(int field=3;field<22;field++){p=strchr(p,' ');if(!p)return 0;p++;}
 return strtoull(p,NULL,10);
}

static long long mono_ms(void){
 struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
 return (long long)t.tv_sec*1000+t.tv_nsec/1000000;
}
static void signal_stop(int sig){(void)sig;stopping=1;}
static void pause_ms(unsigned n){
 long long end=mono_ms()+n;
 while(!stopping&&mono_ms()<end){struct timespec t={0,20000000};nanosleep(&t,NULL);}
}
static bool join(char *out,size_t cap,const char *base,const char *name){
 int n=snprintf(out,cap,"%s/%s",base,name);return n>0&&(size_t)n<cap;
}
static bool regular_readable(const char *path){
 struct stat st;return stat(path,&st)==0&&S_ISREG(st.st_mode)&&access(path,R_OK)==0;
}
static bool child_alive(pid_t *pid){
 if(*pid<=0)return false;
 int status;pid_t r=waitpid(*pid,&status,WNOHANG);
 if(r==0)return true;
 if(r==*pid||(r<0&&errno==ECHILD))*pid=0;
 return *pid>0;
}
static pid_t spawn(char *const argv[],int output){
 pid_t parent=getpid();pid_t pid=fork();if(pid!=0)return pid;
 /* Kernel-owned lifetime: abrupt manager death cannot leave WPA/DHCP behind. */
 if(prctl(PR_SET_PDEATHSIG,SIGKILL)||getppid()!=parent)_exit(126);
 /* flock belongs to the manager only, never its helpers. */
 if(lock_fd>=0)close(lock_fd);
 struct sigaction sa={0};sa.sa_handler=SIG_DFL;sigemptyset(&sa.sa_mask);
 sigaction(SIGTERM,&sa,NULL);sigaction(SIGINT,&sa,NULL);
 if(!getenv("LD_LIBRARY_PATH"))setenv("LD_LIBRARY_PATH","/root/lib:/lib:/usr/lib",1);
 int null=open("/dev/null",O_RDWR);
 if(null>=0){dup2(null,STDIN_FILENO);dup2(null,STDERR_FILENO);dup2(output>=0?output:null,STDOUT_FILENO);if(null>2)close(null);}
 if(output>2)close(output);
 execv(argv[0],argv);_exit(127);
}
static void stop_child(pid_t *pid){
 if(*pid<=0)return;
 pid_t own=*pid;kill(own,SIGTERM);
 long long end=mono_ms()+1000;
 while(child_alive(pid)&&mono_ms()<end){struct timespec t={0,20000000};nanosleep(&t,NULL);}
 if(*pid>0){kill(own,SIGKILL);while(waitpid(own,NULL,0)<0&&errno==EINTR){}*pid=0;}
}
/* Bounded helper execution; status/error output can never contain credentials in logs. */
static bool capture(char *const argv[],char *out,size_t cap){
 int fds[2];if(pipe(fds))return false;
 fcntl(fds[0],F_SETFD,FD_CLOEXEC);fcntl(fds[1],F_SETFD,FD_CLOEXEC);
 pid_t pid=spawn(argv,fds[1]);close(fds[1]);
 if(pid<0){close(fds[0]);return false;}
 fcntl(fds[0],F_SETFL,O_NONBLOCK);
 size_t len=0;bool overflow=false,done=false;int status=0;
 long long end=mono_ms()+3000;
 while(!stopping&&mono_ms()<end){
  char b[512];ssize_t n;
  while((n=read(fds[0],b,sizeof b))>0){
   if((size_t)n>=cap-len){overflow=true;}else if(!overflow){memcpy(out+len,b,(size_t)n);len+=(size_t)n;}
  }
  pid_t r=waitpid(pid,&status,WNOHANG);
  if(r==pid){done=true;break;}
  if(r<0&&errno!=EINTR)break;
  struct timespec t={0,20000000};nanosleep(&t,NULL);
 }
 /* A final drain covers a child exiting between the read and waitpid. */
 char b[512];ssize_t n;
 while((n=read(fds[0],b,sizeof b))>0){if((size_t)n>=cap-len)overflow=true;else if(!overflow){memcpy(out+len,b,(size_t)n);len+=(size_t)n;}}
 close(fds[0]);out[len]='\0';
 if(!done)stop_child(&pid);
 return done&&!overflow&&WIFEXITED(status)&&WEXITSTATUS(status)==0;
}
static bool publish(const char *state,const char *ip,unsigned failures){
 char tmp[1100];int n=snprintf(tmp,sizeof tmp,"%s.new",state_path);
 if(n<0||(size_t)n>=sizeof tmp)return false;
 int fd=open(tmp,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);if(fd<0)return false;
 FILE *f=fdopen(fd,"w");if(!f){close(fd);return false;}
 bool ok=fprintf(f,"{\"state\":\"%s\",\"interface\":\"%s\",\"ipv4\":\"%s\","
  "\"manager_pid\":%ld,\"wpa_pid\":%ld,\"dhcp_pid\":%ld,\"retry_count\":%u,"
  "\"manager_start\":%llu,\"wpa_start\":%llu,\"dhcp_start\":%llu,"
  "\"updated_at\":%lld,\"monotonic_ms\":%lld,\"wan_checked\":false}\n",
  state,iface,ip,(long)getpid(),(long)wpa_pid,(long)dhcp_pid,failures,
  start_ticks(getpid()),start_ticks(wpa_pid),start_ticks(dhcp_pid),(long long)time(NULL),mono_ms())>0;
 if(fclose(f))ok=false;
 if(ok&&rename(tmp,state_path)==0)return true;
 unlink(tmp);return false;
}
/* Refuse coexistence, including foreign WPA/DHCP on another interface. Conservative
 * until per-interface handover has been validated on the target. Read failure is fatal. */
static pid_t ap_pid, ap_dhcp_pid;
static int foreign_owner(void){
 DIR *d=opendir(procroot);if(!d)return -1;
 struct dirent *e;int found=0;
 while((e=readdir(d))){
  char *end;long pid=strtol(e->d_name,&end,10);if(*end||pid<=0||pid==wpa_pid||pid==dhcp_pid||pid==ap_pid||pid==ap_dhcp_pid)continue;
  char path[1024],rel[64];snprintf(rel,sizeof rel,"%ld/comm",pid);
  if(!join(path,sizeof path,procroot,rel)){found=-1;break;}
  FILE *f=fopen(path,"r");if(!f){if(errno==ENOENT||errno==ESRCH)continue;found=-1;break;}
  char name[64];bool ok=fgets(name,sizeof name,f)!=NULL;fclose(f);if(!ok){
   if(!start_ticks((pid_t)pid))continue;
   found=-1;break;
  }
  name[strcspn(name,"\r\n")]=0;
  if(!strcmp(name,"ygclient")||!strcmp(name,"daemon")||!strcmp(name,"wpa_supplicant")||
     !strcmp(name,"udhcpc")||!strcmp(name,"hostapd")||!strcmp(name,"udhcpd")){found=1;break;}
 }
 closedir(d);return found;
}
static bool field(const char *text,const char *key,char *value,size_t cap){
 size_t k=strlen(key);
 for(const char *p=text;*p;){
  const char *e=strchr(p,'\n');size_t n=e?(size_t)(e-p):strlen(p);
  if(n>k&&memcmp(p,key,k)==0&&p[k]=='='){
   size_t len=n-k-1;if(len>=cap)return false;memcpy(value,p+k+1,len);value[len]=0;return true;
  }
  if(!e)break;
  p=e+1;
 }
 return false;
}
static bool usable_ip(const char *value){
 struct in_addr a;if(inet_pton(AF_INET,value,&a)!=1)return false;
 unsigned n=ntohl(a.s_addr);
 return (n>>24)!=0&&(n>>24)!=127&&(n>>24)<224&&(n>>16)!=0xa9fe;
}
static unsigned backoff(unsigned failures){
 unsigned shift=failures>6?6:failures;unsigned long n=(unsigned long)retry_ms<<shift;
 return n>60000?60000:(unsigned)n;
}
static bool path_option(const char *s){return s[0]=='/'&&strlen(s)<900;}
static unsigned number(const char *s){
 char *end;unsigned long n=strtoul(s,&end,10);return *s&&!*end&&n>=20&&n<=60000?(unsigned)n:0;
}
#include "zigma_pairing.h"
int main(int argc,char **argv){
 if(zigma_version_only(argc,argv,"zigma-network-arm"))return 0;
 bool run=false;
 for(int i=1;i<argc;i++){
  const char *a=argv[i];if(!strcmp(a,"--run")){run=true;continue;}
  if(i+1==argc){fprintf(stderr,"Option requires a value\n");return 2;}const char *v=argv[++i];
  if(!strcmp(a,"--interface")){iface=v;continue;}
  if(!strcmp(a,"--poll-ms")){if(!(poll_ms=number(v)))return 2;continue;}
  if(!strcmp(a,"--retry-ms")){if(!(retry_ms=number(v)))return 2;continue;}
  if(!strcmp(a,"--ssh-keys")){if(*v!='/')return 2;ssh_keys=v;continue;}
  if(!strcmp(a,"--pairing-dns-port")){char *end;unsigned long n=strtoul(v,&end,10);if(!*v||*end||!n||n>65535)return 2;dns_port=(unsigned)n;continue;}
  if(!strcmp(a,"--pairing-port")){char *end;unsigned long n=strtoul(v,&end,10);if(!*v||*end||!n||n>65535)return 2;ap_port=(unsigned)n;continue;}
  if(!strcmp(a,"--pairing-ip")){/* Loopback override for isolated fixtures only. */
   if(strcmp(v,"192.168.99.1")&&strcmp(v,"127.0.0.1"))return 2;
   ap_ip=v;continue;
  }
  if(!strcmp(a,"--pairing-timeout-ms")){char *end;unsigned long n=strtoul(v,&end,10);if(!*v||*end||n<1000||n>300000)return 2;ap_timeout=(unsigned)n;continue;}
  if(!strcmp(a,"--pairing-test-ms")){if(!(test_timeout=number(v)))return 2;continue;}
  if(!strcmp(a,"--scan-timeout-ms")){if(!(scan_timeout=number(v)))return 2;continue;}
  if(!strcmp(a,"--pairing-stable-ms")){if(!(stable_ms=number(v)))return 2;continue;}
  if(!path_option(v))return 2;
  if(!strcmp(a,"--config"))config=v;
  else if(!strcmp(a,"--runtime"))runtime=v;
  else if(!strcmp(a,"--wpa"))wpa=v;
  else if(!strcmp(a,"--wpa-cli"))cli=v;
  else if(!strcmp(a,"--iwlist"))scan_bin=v;
  else if(!strcmp(a,"--dhcp"))dhcp=v;
  else if(!strcmp(a,"--ifconfig"))ifconfig_bin=v;
  else if(!strcmp(a,"--dhcp-hook"))hook=v;
  else if(!strcmp(a,"--proc-root"))procroot=v;
  else if(!strcmp(a,"--hostapd"))ap_bin=v;
  else if(!strcmp(a,"--udhcpd"))ap_dhcp=v;
  else {fprintf(stderr,"Unknown option\n");return 2;}
 }
 if(!run){fprintf(stderr,"Use --run to manage Wi-Fi (foreground). See NETWORK.md.\n");return 2;}
 if(!*iface||strlen(iface)>=IF_NAMESIZE)return 2;
 for(const char *s=iface;*s;s++)if(!isalnum((unsigned char)*s)&&*s!='_'&&*s!='-'&&*s!='.')return 2;
 umask(077);
 if(mkdir(runtime,0700)&&errno!=EEXIST){perror("runtime");return 1;}
 struct stat st;if(lstat(runtime,&st)||!S_ISDIR(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&0077)){
  fprintf(stderr,"Runtime must be an owner-only directory\n");return 1;
 }
 char path[1024];if(!join(path,sizeof path,runtime,"manager.lock")||!join(state_path,sizeof state_path,runtime,"status.json")||
  !join(dhcp_path,sizeof dhcp_path,runtime,"udhcpc.pid"))return 2;
 lock_fd=open(path,O_RDWR|O_CREAT|O_NOFOLLOW,0600);
 if(lock_fd<0||flock(lock_fd,LOCK_EX|LOCK_NB)){fprintf(stderr,"Network manager already running or lock unavailable\n");return 1;}
 fcntl(lock_fd,F_SETFD,FD_CLOEXEC);
 struct sigaction sa={0};sa.sa_handler=signal_stop;sigemptyset(&sa.sa_mask);
 sigaction(SIGTERM,&sa,NULL);sigaction(SIGINT,&sa,NULL);
 if(!pairing_init()){fprintf(stderr,"Cannot initialize pairing listener\n");return 1;}
 unsigned failures=0;long long retry_at=0;bool fatal=false;
 while(!stopping){
  const char *state="interface_wait";char ip[64]="";
  int foreign=foreign_owner();
  bool request=pairing_requested();
  if(pair_phase!=PAIR_IDLE&&(foreign||mono_ms()>=pair_deadline)){
   pairing_cancel();retry_at=0;failures=0;
  }
  if(request&&pair_phase==PAIR_IDLE&&!foreign&&if_nametoindex(iface)){
   scan_options[0]=0;scan_count=0;pair_error[0]=0;pair_deadline=mono_ms()+ap_timeout;
   if(!ap_start()){pairing_cancel();retry_at=0;}
  }
  if(pair_phase==PAIR_AP){
   if(!child_alive(&ap_pid)||!child_alive(&ap_dhcp_pid)){pairing_cancel();retry_at=0;}
   else{
    int action=portal_step();
    if(action==2){pairing_cancel();retry_at=0;}
    else if(action==1){
     ap_stop();
     if(!clear_address()){pairing_cancel();retry_at=0;}
     else{pair_phase=PAIR_TEST;test_deadline=mono_ms()+test_timeout;stable_since=0;stable_ip[0]=0;retry_at=0;failures=0;}
    }
    if(pair_phase==PAIR_AP){
     if(!publish("pairing_ap",ap_ip,0)){fatal=true;break;}
     pause_ms(50);continue;
    }
   }
  }
  if(pair_phase==PAIR_TEST&&mono_ms()>=test_deadline){
   snprintf(pair_error,sizeof pair_error,"Connection failed. Check your network name and password, then try again.");
   if(!ap_start()){pairing_cancel();retry_at=0;}
   continue;
  }
  const char *active_config=pair_phase==PAIR_TEST?candidate:config;
  if(foreign){
   stop_child(&dhcp_pid);stop_child(&wpa_pid);
   state=foreign>0?"ownership_conflict":"process_scan_failed";
  }else if(!regular_readable(active_config)){
   stop_child(&dhcp_pid);stop_child(&wpa_pid);state="config_missing";
  }else if(!regular_readable(hook)||access(wpa,X_OK)||access(cli,X_OK)||access(dhcp,X_OK)||access(ifconfig_bin,X_OK)){
   stop_child(&dhcp_pid);stop_child(&wpa_pid);state="dependency_missing";
  }else if(!if_nametoindex(iface)){
   stop_child(&dhcp_pid);stop_child(&wpa_pid);
  }else{
   if(!child_alive(&wpa_pid)){
    stop_child(&dhcp_pid);
    if(mono_ms()>=retry_at){
     char out[128];char *up[]={(char*)ifconfig_bin,(char*)iface,"up",NULL};
     if(capture(up,out,sizeof out)){
      char *args[]={(char*)wpa,"-D","nl80211","-i",(char*)iface,"-c",(char*)active_config,NULL};
      wpa_pid=spawn(args,-1);if(wpa_pid<0)wpa_pid=0;
     }
     retry_at=mono_ms()+backoff(failures);if(failures<1000000)failures++;
    }
    state="wpa_starting";
   }else{
    char out[4096],value[64];char *args[]={(char*)cli,"-i",(char*)iface,"status",NULL};
    bool associated=capture(args,out,sizeof out)&&field(out,"wpa_state",value,sizeof value)&&!strcmp(value,"COMPLETED");
    if(!associated){state="associating";/* WPA reconnects; do not tear down DHCP on a transient radio loss. */}
    else{
     state="dhcp_wait";
     if(!child_alive(&dhcp_pid)&&mono_ms()>=retry_at){
      char *dargs[]={(char*)dhcp,"-f","-i",(char*)iface,"-s",(char*)hook,"-p",dhcp_path,"-x","hostname:ROBOT",NULL};
      dhcp_pid=spawn(dargs,-1);if(dhcp_pid<0)dhcp_pid=0;
      retry_at=mono_ms()+backoff(failures);if(failures<1000000)failures++;
     }
     if(child_alive(&dhcp_pid)&&field(out,"ip_address",value,sizeof value)&&usable_ip(value)){
      snprintf(ip,sizeof ip,"%s",value);state="lan_addressed";failures=0;
     }
    }
   }
  }
  if(pair_phase==PAIR_TEST){
   if(!strcmp(state,"lan_addressed")&&strcmp(ip,ap_ip)&&strcmp(ip,"192.168.99.1")){
    if(!stable_since||strcmp(stable_ip,ip)){stable_since=mono_ms();snprintf(stable_ip,sizeof stable_ip,"%s",ip);}
    if(mono_ms()-stable_since>=stable_ms){
     if(commit_candidate()){
      pair_phase=PAIR_IDLE;unlink(ap_mark);
      /* Restart WPA against the durable path; later reconfigure never depends on RAM. */
      stop_child(&dhcp_pid);stop_child(&wpa_pid);unlink(candidate);retry_at=0;state="wpa_starting";ip[0]=0;
     }else{
      snprintf(pair_error,sizeof pair_error,"Could not store Wi-Fi settings. Please try again.");
      if(!ap_start()){pairing_cancel();retry_at=0;}
      continue;
     }
    }
   }else stable_since=0;
   if(pair_phase==PAIR_TEST)state="pairing_connecting";
  }
  if(!publish(state,ip,failures)){fprintf(stderr,"Cannot publish network state\n");fatal=true;break;}
  pause_ms(pair_phase==PAIR_TEST?100:poll_ms);
 }
 if(pair_phase!=PAIR_IDLE)pairing_cancel();
 ap_stop();if(pair_socket>=0)close(pair_socket);unlink(pair_path);
 stop_child(&dhcp_pid);stop_child(&wpa_pid);unlink(dhcp_path);
 if(!publish(fatal?"failed":"stopped","",failures))fatal=true;
 close(lock_fd);return fatal?1:0;
}
