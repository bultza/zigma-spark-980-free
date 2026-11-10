/* Bounded services available only during physically requested setup. */
#include <ctype.h>
#include "zigma_ssh_key.h"
static int dns_fd=-1;
static unsigned dns_port=53;
static const char *ssh_keys="/root/.ssh/authorized_keys";
#include "zigma_pairing_scan.h"
static int pending_action;
static long long action_at;

static bool dns_start(void){
 dns_fd=socket(AF_INET,SOCK_DGRAM,0);if(dns_fd<0)return false;
 fcntl(dns_fd,F_SETFD,FD_CLOEXEC);fcntl(dns_fd,F_SETFL,O_NONBLOCK);
 struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons((unsigned short)dns_port)};
 return inet_pton(AF_INET,ap_ip,&a.sin_addr)==1&&bind(dns_fd,(struct sockaddr*)&a,sizeof a)==0;
}
static void dns_step(void){
 /* RFC1035 single-question standard queries. No forwarding or compression
  * traversal; malformed, oversized and response packets are discarded. */
 for(unsigned i=0;i<8;i++){
  unsigned char b[513];struct sockaddr_in peer;socklen_t len=sizeof peer;
  ssize_t got=recvfrom(dns_fd,b,sizeof b,0,(struct sockaddr*)&peer,&len);if(got<0)break;
  if(got<17||got>512||(b[2]&0xf8)||b[4]||b[5]!=1||b[6]||b[7]||b[8]||b[9])continue;
  size_t end=12;bool ok=true;
  while(end<(size_t)got&&b[end]){unsigned n=b[end++];if(n>63||end+n>=(size_t)got||end+n>266){ok=false;break;}end+=n;}
  if(!ok||end+5>(size_t)got)continue;
  end++;unsigned type=(unsigned)b[end]*256+b[end+1],cls=(unsigned)b[end+2]*256+b[end+3];end+=4;
  bool answer=type==1&&cls==1;
  b[2]=(b[2]&1)|0x80;b[3]=0x80;b[6]=0;b[7]=answer?1:0;memset(b+8,0,4);
  if(answer){const unsigned char rr[]={0xc0,0x0c,0,1,0,1,0,0,0,0,0,4};memcpy(b+end,rr,sizeof rr);end+=sizeof rr;struct in_addr ip;inet_pton(AF_INET,ap_ip,&ip);memcpy(b+end,&ip,4);end+=4;}
  sendto(dns_fd,b,end,MSG_NOSIGNAL,(struct sockaddr*)&peer,len);
 }
}
static bool ssh_listening(void){
 FILE *f=fopen("/proc/net/tcp","r");if(!f)return false;
 char row[512];bool found=false;struct in_addr ip;inet_pton(AF_INET,ap_ip,&ip);
 while(fgets(row,sizeof row,f)){unsigned local,port,state;if(sscanf(row," %*u: %x:%x %*s %x",&local,&port,&state)==3&&port==22&&state==10&&(local==0||local==ip.s_addr)){found=true;break;}}
 fclose(f);return found;
}
static bool ssh_upload(char *key){
 size_t length=strlen(key);while(length&&isspace((unsigned char)key[length-1]))key[--length]=0;
 if(!length||length>8192||strchr(key,'\n')||strchr(key,'\r')||strstr(key,"PRIVATE KEY"))return false;
 char *data=strchr(key,' ');if(!data)return false;*data++=0;while(*data==' ')data++;
 char *comment=strchr(data,' ');if(comment)*comment=0;
 size_t encoded_len=strlen(data);if(!encoded_len||encoded_len%4)return false;
 size_t pad=0;while(pad<encoded_len&&data[encoded_len-1-pad]=='=')pad++;
 if(pad>2)return false;
 unsigned char decoded[6144];size_t n=0;unsigned bits=0,value=0;bool padding=false;
 for(const char *p=data;*p;p++){
  if(*p=='='){padding=true;continue;}if(padding)return false;
  const char *alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/",*at=strchr(alphabet,*p);
  if(!at||n>=sizeof decoded)return false;
  value=(value<<6)|(unsigned)(at-alphabet);bits+=6;if(bits>=8){bits-=8;decoded[n++]=(unsigned char)(value>>bits);}
 }
 if((bits&&(value&((1u<<bits)-1)))||!public_key_valid(key,decoded,n))return false;
 char line[8400];int size=snprintf(line,sizeof line,"%s %s",key,data);if(size<0||(size_t)size>=sizeof line)return false;
 /* Never replace a symlink or discard existing keys. Atomic sibling rename. */
 char next[1024],parent[1024];if(snprintf(next,sizeof next,"%s.pairing-new",ssh_keys)>=(int)sizeof next)return false;
 snprintf(parent,sizeof parent,"%s",ssh_keys);char *slash=strrchr(parent,'/');if(!slash)return false;*slash=0;
 struct stat st;if(lstat(parent,&st)||!S_ISDIR(st.st_mode))return false;
 int dir=open(parent,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(dir<0)return false;
 char *old=calloc(1,65537);if(!old){close(dir);return false;}size_t used=0;bool ok=true;
 int fd=open(ssh_keys,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
 if(fd<0){if(errno!=ENOENT)ok=false;}else{
  if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<0||st.st_size>65536)ok=false;
  while(ok&&used<(size_t)st.st_size){ssize_t got=read(fd,old+used,(size_t)st.st_size-used);if(got<0&&errno==EINTR)continue;if(got<=0){ok=false;break;}used+=(size_t)got;}close(fd);
 }
 if(ok&&memchr(old,0,used))ok=false;
 bool duplicate=false;
 if(ok)for(char *p=old;*p;){size_t l=strcspn(p,"\n");if(l>=(size_t)size&&!memcmp(p,line,(size_t)size)&&(l==(size_t)size||isspace((unsigned char)p[size])))duplicate=true;p+=l;if(*p)p++;}
 if(ok&&!duplicate){char *text=malloc(used+(size_t)size+3);if(!text)ok=false;else{memcpy(text,old,used);if(used&&text[used-1]!='\n')text[used++]='\n';memcpy(text+used,line,(size_t)size);used+=(size_t)size;text[used++]='\n';text[used]=0;ok=private_write(next,text)&&rename(next,ssh_keys)==0&&fsync(dir)==0;free(text);}}
 if(!ok)unlink(next);
 free(old);close(dir);return ok;
}
