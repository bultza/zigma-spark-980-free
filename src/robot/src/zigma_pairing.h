/* Local, physically requested provisioning. Included by the sole network owner. */
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <sys/time.h>
#include <strings.h>

static const char *ap_bin="/root/bin/hostapd", *ap_dhcp="/usr/sbin/udhcpd";
static const char *ap_ip="192.168.99.1";
static unsigned ap_port=80, ap_timeout=300000, test_timeout=60000, stable_ms=3000;
static int pair_socket=-1, portal=-1;
static char pair_path[1024], candidate[1024], ap_config[1024], ap_dhcp_config[1024], ap_mark[1024];
static char csrf[65], ap_name[40], pair_error[100];
static enum {PAIR_IDLE,PAIR_AP,PAIR_TEST} pair_phase;
static long long pair_deadline, test_deadline, stable_since;
static char stable_ip[64];

static bool private_write(const char *path,const char *text){
 int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);if(fd<0)return false;
 bool ok=fchmod(fd,0600)==0;size_t left=strlen(text);const char *p=text;
 while(ok&&left){ssize_t n=write(fd,p,left);if(n<0&&errno==EINTR)continue;if(n<=0){ok=false;break;}left-=(size_t)n;p+=n;}
 if(fsync(fd))ok=false;
 if(close(fd))ok=false;
 return ok;
}
static int unhex(unsigned char c);
#include "zigma_pairing_services.h"
static bool clear_address(void){
 char out[128];char *args[]={(char*)ifconfig_bin,(char*)iface,"0.0.0.0",NULL};
 return capture(args,out,sizeof out);
}
static void ap_stop(void){
 scan_stop();
 if(portal>=0){close(portal);portal=-1;}
 if(dns_fd>=0){close(dns_fd);dns_fd=-1;}
 pending_action=0;
 stop_child(&ap_dhcp_pid);stop_child(&ap_pid);
}
static void pairing_cancel(void){
 ap_stop();stop_child(&dhcp_pid);stop_child(&wpa_pid);
 clear_address();unlink(candidate);unlink(ap_mark);
 pair_phase=PAIR_IDLE;stable_since=0;
}
static bool random_token(void){
 unsigned char b[32];int fd=open("/dev/urandom",O_RDONLY|O_CLOEXEC);if(fd<0)return false;
 ssize_t n=read(fd,b,sizeof b);close(fd);if(n!=sizeof b)return false;
 for(unsigned i=0;i<sizeof b;i++)snprintf(csrf+2*i,3,"%02x",b[i]);
 return true;
}
static bool ap_start(void){
 ap_stop();stop_child(&dhcp_pid);stop_child(&wpa_pid);unlink(candidate);
 if(!random_token()||access(ap_bin,X_OK)||access(ap_dhcp,X_OK)||!clear_address())return false;
 /* Stable suffix from interface MAC; no account or cloud identity involved. */
 char macpath[128],mac[64]="";snprintf(macpath,sizeof macpath,"/sys/class/net/%s/address",iface);
 FILE *m=fopen(macpath,"r");if(m){if(!fgets(mac,sizeof mac,m))mac[0]=0;fclose(m);}
 unsigned hash=2166136261u;for(const char *p=mac;*p;p++)hash=(hash^(unsigned char)*p)*16777619u;
 snprintf(ap_name,sizeof ap_name,"ROBOT-%06X",hash&0xffffff);
 char text[4096];snprintf(text,sizeof text,
  "interface=%s\ndriver=nl80211\nssid=%s\nhw_mode=g\nchannel=1\nauth_algs=1\nwpa=0\nignore_broadcast_ssid=0\n",iface,ap_name);
 if(!private_write(ap_config,text))return false;
 snprintf(text,sizeof text,"start 192.168.99.100\nend 192.168.99.199\ninterface %s\nmax_leases 16\npidfile %s/ap-dhcp.pid\nlease_file %s/ap.leases\noption subnet 255.255.255.0\noption router %s\noption dns %s\noption lease 300\nauto_time 0\n",iface,runtime,runtime,ap_ip,ap_ip);
 if(!private_write(ap_dhcp_config,text)||!private_write(ap_mark,"active\n"))return false;
 char out[128];char *addr[]={(char*)ifconfig_bin,(char*)iface,(char*)ap_ip,"netmask","255.255.255.0","up",NULL};
 if(!capture(addr,out,sizeof out))return false;
 portal=socket(AF_INET,SOCK_STREAM,0);if(portal<0)return false;
 fcntl(portal,F_SETFD,FD_CLOEXEC);int yes=1;setsockopt(portal,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof yes);
 struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons((unsigned short)ap_port)};
 if(inet_pton(AF_INET,ap_ip,&a.sin_addr)!=1||bind(portal,(struct sockaddr*)&a,sizeof a)||listen(portal,4))return false;
 fcntl(portal,F_SETFL,O_NONBLOCK);
 if(!dns_start())return false;
 char *ha[]={(char*)ap_bin,ap_config,NULL};ap_pid=spawn(ha,-1);
 char *da[]={(char*)ap_dhcp,"-f",ap_dhcp_config,NULL};ap_dhcp_pid=spawn(da,-1);
 if(ap_pid<=0||ap_dhcp_pid<=0)return false;
 pair_phase=PAIR_AP;stable_since=0;scan_start(500);return true;
}
static bool pairing_init(void){
 if(!join(pair_path,sizeof pair_path,runtime,"pairing.sock")||strlen(pair_path)>=sizeof(((struct sockaddr_un*)0)->sun_path)||
    !join(candidate,sizeof candidate,runtime,"candidate.conf")||!join(ap_config,sizeof ap_config,runtime,"hostapd.conf")||
    !join(ap_dhcp_config,sizeof ap_dhcp_config,runtime,"udhcpd.conf")||!join(ap_mark,sizeof ap_mark,runtime,"ap-active"))return false;
 /* A killed AP owner must not leave its static address masquerading as DHCP. */
 if(access(ap_mark,F_OK)==0){if(!clear_address())return false;unlink(ap_mark);}
 unlink(candidate);unlink(pair_path);
 pair_socket=socket(AF_UNIX,SOCK_DGRAM,0);if(pair_socket<0)return false;
 fcntl(pair_socket,F_SETFD,FD_CLOEXEC);fcntl(pair_socket,F_SETFL,O_NONBLOCK);
 struct sockaddr_un a={.sun_family=AF_UNIX};memcpy(a.sun_path,pair_path,strlen(pair_path)+1);
 return bind(pair_socket,(struct sockaddr*)&a,sizeof a)==0&&chmod(pair_path,0600)==0;
}
static bool pairing_requested(void){
 char data[80];bool requested=false;
 for(unsigned i=0;i<16;i++){
  ssize_t n=recv(pair_socket,data,sizeof data-1,0);if(n<0)break;data[n]=0;
  long long when=0;char extra;
  if(sscanf(data,"PAIR %lld %c",&when,&extra)==1&&when>0&&mono_ms()>=when&&mono_ms()-when<10000)requested=true;
 }
 return requested;
}
static int unhex(unsigned char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
static bool decode_form(const char *in,size_t len,char *out,size_t cap,bool multiline){
 size_t used=0;for(size_t i=0;i<len;i++){
  unsigned char c=(unsigned char)in[i];
  if(c=='%'){if(i+2>=len)return false;int a=unhex(in[i+1]),b=unhex(in[i+2]);if(a<0||b<0)return false;c=(unsigned char)(a*16+b);i+=2;}
  else if(c=='+')c=' ';
  if((c<32&&!(multiline&&(c==10||c==13)))||c==127||used+1>=cap)return false;
  out[used++]=(char)c;
 }
 out[used]=0;return true;
}
static bool form_fields(char *body,char *ssid,char *password,char *token,char *key){
 unsigned seen=0;char *save=NULL;
 for(char *p=strtok_r(body,"&",&save);p;p=strtok_r(NULL,"&",&save)){
  char *eq=strchr(p,'=');if(!eq)return false;*eq++=0;
  unsigned bit;char *out;size_t cap;
  if(!strcmp(p,"ssid")){bit=1;out=ssid;cap=33;}
  else if(!strcmp(p,"password")){bit=2;out=password;cap=64;}
  else if(!strcmp(p,"token")){bit=4;out=token;cap=65;}
  else if(!strcmp(p,"public_key")){bit=8;out=key;cap=8193;}
  else return false;
  if(seen&bit||!decode_form(eq,strlen(eq),out,cap,bit==8))return false;
  seen|=bit;
 }
 return (seen&4)!=0;
}
static bool candidate_write(const char *ssid,const char *password){
 if(!*ssid||strlen(ssid)>32||strlen(password)<8||strlen(password)>63)return false;
 char hex[65],escaped[128];size_t j=0;
 for(size_t i=0;i<strlen(ssid);i++)snprintf(hex+2*i,3,"%02x",(unsigned char)ssid[i]);
 for(const char *p=password;*p;p++){if(*p=='\\'||*p=='"')escaped[j++]='\\';escaped[j++]=*p;}escaped[j]=0;
 char text[512];snprintf(text,sizeof text,"ctrl_interface=/var/run/wpa_supplicant\nupdate_config=0\nap_scan=1\nnetwork={\n scan_ssid=1\n ssid=%s\n psk=\"%s\"\n key_mgmt=WPA-PSK\n}\n",hex,escaped);
 bool ok=private_write(candidate,text);memset(text,0,sizeof text);memset(escaped,0,sizeof escaped);return ok;
}
#include "zigma_pairing_portal.h"
static bool commit_candidate(void){
 char data[1024];int fd=open(candidate,O_RDONLY|O_NOFOLLOW);if(fd<0)return false;
 ssize_t n=read(fd,data,sizeof data-1);close(fd);if(n<=0||n==(ssize_t)sizeof data-1)return false;data[n]=0;
 char next[1024],previous[1024],backup_new[1024],parent[1024];
 if(snprintf(next,sizeof next,"%s.pairing-new",config)>=(int)sizeof next||
    snprintf(previous,sizeof previous,"%s.pairing-previous",config)>=(int)sizeof previous||
    snprintf(backup_new,sizeof backup_new,"%s.pairing-backup-new",config)>=(int)sizeof backup_new)return false;
 snprintf(parent,sizeof parent,"%s",config);char *slash=strrchr(parent,'/');if(!slash)return false;if(slash==parent)slash[1]=0;else *slash=0;
 int dir=open(parent,O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(dir<0)return false;
 /* Keep just the previous Wi-Fi file, never a general robot backup. */
 bool existed=false,ok=true;fd=open(config,O_RDONLY|O_NOFOLLOW);
 if(fd>=0){
  existed=true;struct stat st;char *old=NULL;
  if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<0||st.st_size>65536)ok=false;
  if(ok){old=malloc((size_t)st.st_size+1);if(!old)ok=false;}
  if(ok){
   size_t used=0;while(used<(size_t)st.st_size){ssize_t got=read(fd,old+used,(size_t)st.st_size-used);if(got<0&&errno==EINTR)continue;if(got<=0){ok=false;break;}used+=(size_t)got;}
   if(ok){old[used]=0;ok=!memchr(old,0,used)&&private_write(backup_new,old)&&rename(backup_new,previous)==0;}
   memset(old,0,(size_t)st.st_size);
  }
  free(old);close(fd);
 }else if(errno!=ENOENT)ok=false;
 if(ok)ok=fsync(dir)==0&&private_write(next,data);
 memset(data,0,sizeof data);
 if(ok)ok=rename(next,config)==0;
 if(ok&&fsync(dir)){
  /* Restore the old durable name on a directory-sync failure. Hardware/media
   * failure during rollback itself cannot be repaired by a software guarantee. */
  if(existed)rename(previous,config);else unlink(config);
  fsync(dir);ok=false;
 }
 close(dir);if(!ok){unlink(next);unlink(backup_new);}return ok;
}
