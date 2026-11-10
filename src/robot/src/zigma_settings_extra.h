/* Settings file operations are enabled only by explicit deployment paths. */
#include <dirent.h>
#include <stdint.h>
#include <sys/statvfs.h>
static const char *voices_dir=NULL,*ssh_keys_file=NULL,*options_bin=NULL;
#define VOICE_MAX (512*1024)
static unsigned char *binary_read(const char *file,size_t max,size_t *len){
 int fd=open(file,O_RDONLY|O_NOFOLLOW);struct stat st;
 if(fd<0)return NULL;
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||(size_t)st.st_size>max){close(fd);return NULL;}
 unsigned char *data=malloc((size_t)st.st_size+1);size_t off=0;
 if(!data){close(fd);return NULL;}
 while(off<(size_t)st.st_size){ssize_t n=read(fd,data+off,(size_t)st.st_size-off);if(n<=0){free(data);close(fd);return NULL;}off+=(size_t)n;}
 close(fd);data[off]=0;*len=off;return data;
}
static bool binary_write(const char *file,const unsigned char *data,size_t len,bool immutable){
 char tmp[1100];if(snprintf(tmp,sizeof(tmp),"%s.new.%ld",file,(long)getpid())>=(int)sizeof(tmp))return false;
 int fd=open(tmp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);if(fd<0)return false;
 size_t off=0;bool ok=true;while(off<len){ssize_t n=write(fd,data+off,len-off);if(n<=0){ok=false;break;}off+=(size_t)n;}
 if(ok&&immutable&&fchmod(fd,0444))ok=false;
 if(ok&&fsync(fd))ok=false;
 if(close(fd))ok=false;
 if(ok){if(immutable){if(link(tmp,file))ok=false;}else if(rename(tmp,file))ok=false;}
 unlink(tmp);return ok;
}
static unsigned char *base64_decode(const char *s,size_t max,size_t *size){
 const char *abc="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";size_t len=strlen(s),n=0;
 if(!len||len%4||len/4*3>max+2)return NULL;
 unsigned char *out=malloc(len/4*3);if(!out)return NULL;
 for(size_t i=0;i<len;i+=4){unsigned a[4];for(int j=0;j<4;j++){const char *p=strchr(abc,s[i+j]);if(s[i+j]=='=')a[j]=0;else if(p)a[j]=(unsigned)(p-abc);else goto bad;}
  if(s[i]=='='||s[i+1]=='='||(s[i+2]=='='&&s[i+3]!='=')||((s[i+2]=='='||s[i+3]=='=')&&i+4!=len))goto bad;
  if(s[i+2]=='='&&(a[1]&15))goto bad;
  if(s[i+3]=='='&&s[i+2]!='='&&(a[2]&3))goto bad;
  out[n++]=(unsigned char)(a[0]<<2|a[1]>>4);if(s[i+2]!='=')out[n++]=(unsigned char)(a[1]<<4|a[2]>>2);if(s[i+3]!='=')out[n++]=(unsigned char)(a[2]<<6|a[3]);
 }
 if(n>max)goto bad;
 *size=n;return out;
bad:free(out);return NULL;
}
static bool voice_name(const char *name){size_t n=strlen(name);if(n<5||n>160||strcmp(name+n-4,".mp3"))return false;for(size_t i=0;i<n-4;i++)if(!isalnum((unsigned char)name[i])&&name[i]!='_'&&name[i]!='-')return false;return true;}
/* Require a complete sequence of MPEG layer III frames, not just a file suffix. */
static bool mp3_valid(const unsigned char *b,size_t n){
 size_t p=0;int frames=0;
 if(n>=10&&!memcmp(b,"ID3",3)){if(b[3]<2||b[3]>4||(b[6]|b[7]|b[8]|b[9])&128)return false;p=10+((size_t)b[6]<<21)+((size_t)b[7]<<14)+((size_t)b[8]<<7)+b[9];if(b[3]==4&&(b[5]&16))p+=10;if(p>=n)return false;}
 while(p+4<=n){
  if(n-p==128&&!memcmp(b+p,"TAG",3))return frames>=2;
  const unsigned char *h=b+p;unsigned ver=(h[1]>>3)&3,rate=(h[2]>>2)&3,br=h[2]>>4;
  if(h[0]!=255||(h[1]&224)!=224||ver==1||((h[1]>>1)&3)!=1||rate==3||br==0||br==15)return false;
  static const int rates[]={44100,48000,32000},high[]={0,32,40,48,56,64,80,96,112,128,160,192,224,256,320},low[]={0,8,16,24,32,40,48,56,64,80,96,112,128,144,160};
  unsigned hz=rates[rate]/(ver==3?1:ver==2?2:4),kbps=(ver==3?high:low)[br];size_t length=(ver==3?144000:72000)*kbps/hz+((h[2]>>1)&1);
  if(length<4||length>n-p)return false;
  p+=length;frames++;
 }
 return p==n&&frames>=2;
}
static cJSON *voices_list(void){
 cJSON *o=cJSON_CreateObject(),*items=cJSON_AddArrayToObject(o,"items");DIR *d=voices_dir?opendir(voices_dir):NULL;setbool(o,"available",d!=NULL);setnum(o,"max_bytes",VOICE_MAX);
 if(d){struct dirent *e;while((e=readdir(d))){if(!voice_name(e->d_name))continue;char p[1024],bak[1040];struct stat st;snprintf(p,sizeof(p),"%s/%s",voices_dir,e->d_name);if(lstat(p,&st)||!S_ISREG(st.st_mode))continue;cJSON *v=cJSON_CreateObject();setstr(v,"name",e->d_name);setnum(v,"bytes",st.st_size);snprintf(bak,sizeof(bak),"%s.bak",p);setbool(v,"original_available",!lstat(bak,&st)&&S_ISREG(st.st_mode));cJSON_AddItemToArray(items,v);}closedir(d);}return o;
}
static void voice_get(int fd,const char *name){
 if(!voices_dir||!voice_name(name)){error_response(fd,404,"Voice not found");return;}char p[1024];snprintf(p,sizeof(p),"%s/%s",voices_dir,name);size_t len;unsigned char *b=binary_read(p,VOICE_MAX,&len);if(!b){error_response(fd,404,"Voice unavailable");return;}response(fd,200,"audio/mpeg",(char*)b,len);free(b);
}
static void voice_put(int fd,cJSON *input,bool restore){
 const char *name=str(input,"name","");if(!voices_dir||!voice_name(name)){error_response(fd,400,"Choose an existing voice");return;}
 char p[1024],bak[1040];snprintf(p,sizeof(p),"%s/%s",voices_dir,name);snprintf(bak,sizeof(bak),"%s.bak",p);
 size_t oldlen=0,len=0;unsigned char *old=binary_read(p,VOICE_MAX,&oldlen),*b=NULL;
 if(!old){error_response(fd,400,"Existing voice is unavailable");return;}
 if(restore)b=binary_read(bak,VOICE_MAX,&len);else b=base64_decode(str(input,"data",""),VOICE_MAX,&len);
 if(!b||(!restore&&!mp3_valid(b,len))){free(old);free(b);error_response(fd,400,"Upload a complete MP3, up to 512 KB");return;}
 struct stat st;bool exists=!lstat(bak,&st);struct statvfs space;bool ok=(!exists||S_ISREG(st.st_mode))&&!statvfs(voices_dir,&space)&&(unsigned long long)space.f_bavail*space.f_frsize>len+(exists?0:oldlen)+262144;
 if(ok&&!exists)ok=binary_write(bak,old,oldlen,true);
 if(ok)ok=binary_write(p,b,len,false);
 free(old);free(b);
 if(!ok){error_response(fd,500,"Voice not replaced: check free space and original backup");return;}
 event("settings",restore?"Original voice restored":"Voice replaced; original preserved");save_state();json_response(fd,200,voices_list());
}
#include "zigma_ssh_key.h"
static void ssh_add(int fd,cJSON *input){
 if(!ssh_keys_file){error_response(fd,503,"SSH key upload unavailable");return;}
 const char *key=str(input,"public_key","");if(strlen(key)>8192||strstr(key,"PRIVATE KEY")){error_response(fd,400,"Upload one OpenSSH public key");return;}
 char *copy=strdup(key);if(!copy){error_response(fd,500,"Out of memory");return;}size_t n=strlen(copy);while(n&&isspace((unsigned char)copy[n-1]))copy[--n]=0;
 if(strchr(copy,'\n')||strchr(copy,'\r'))goto invalid_key;
 char *space=strchr(copy,' ');if(!space)goto invalid_key;*space++=0;while(*space==' ')space++;char *end=strchr(space,' ');if(end)*end=0;
 size_t bytes;unsigned char *b=base64_decode(space,8192,&bytes);bool valid=b&&public_key_valid(copy,b,bytes);free(b);if(!valid)goto invalid_key;
 char line[8400];snprintf(line,sizeof(line),"%s %s",copy,space);free(copy);copy=NULL;
 struct stat st;bool exists=!lstat(ssh_keys_file,&st);if(exists&&!S_ISREG(st.st_mode)){error_response(fd,400,"SSH key file is not a regular file");return;}
 size_t oldlen=0;unsigned char *old=exists&&st.st_size?binary_read(ssh_keys_file,65536,&oldlen):NULL;
 if(exists&&st.st_size&&!old){error_response(fd,500,"Existing SSH keys could not be read");return;}
 /* Ignore comments when detecting the same key; preserve every existing byte. */
 bool duplicate=false;if(old){char *rows=strdup((char*)old),*save=NULL;for(char *r=strtok_r(rows,"\n",&save);r;r=strtok_r(NULL,"\n",&save)){size_t l=strlen(line);if(!strncmp(r,line,l)&&(r[l]==0||isspace((unsigned char)r[l])))duplicate=true;}free(rows);}
 if(!duplicate){size_t l=strlen(line);unsigned char *next=malloc(oldlen+l+3);if(!next){free(old);error_response(fd,500,"Out of memory");return;}if(oldlen)memcpy(next,old,oldlen);size_t off=oldlen;if(off&&next[off-1]!='\n')next[off++]='\n';memcpy(next+off,line,l);off+=l;next[off++]='\n';bool ok=binary_write(ssh_keys_file,next,off,false);free(next);free(old);if(!ok){error_response(fd,500,"SSH key could not be added");return;}}else free(old);
 event("settings","SSH public key added; existing keys preserved");save_state();cJSON *o=cJSON_CreateObject();setbool(o,"added",!duplicate);json_response(fd,200,o);return;
invalid_key:free(copy);error_response(fd,400,"Use one OpenSSH Ed25519, RSA (2048+ bit), or ECDSA P-256 public key");
}
static cJSON *timezone_list(void){
 cJSON *a=cJSON_CreateArray();cJSON_AddItemToArray(a,cJSON_CreateString("UTC"));FILE *f=fopen("/usr/share/zoneinfo/zone.tab","r");if(!f)return a;char line[512];while(fgets(line,sizeof(line),f)){if(line[0]=='#')continue;char country[16],coordinates[64],zone[128];if(sscanf(line,"%15s %63s %127s",country,coordinates,zone)==3){char pathbuf[256];snprintf(pathbuf,sizeof(pathbuf),"/usr/share/zoneinfo/%s",zone);if(!access(pathbuf,R_OK))cJSON_AddItemToArray(a,cJSON_CreateString(zone));}}fclose(f);return a;
}
