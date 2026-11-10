#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
/* HOSLAM catalog and private master library. Native deletion is an explicit,
   guarded type-9 operation; catalog/preview never send IPC. No cloud access. */
#include "zigma_version.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "cJSON.h"
#define MAX_FILE (2u*1024u*1024u)
#define MAX_MAPS 32
#define GRID_HEADER 120u
struct map { unsigned char *data; size_t size,grid; uint32_t id,w,h,stamp,packed; double resolution; char revision[17]; };
static uint32_t u32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static double f64(const unsigned char *p){uint64_t n=(uint64_t)u32(p)|((uint64_t)u32(p+4)<<32);double v;memcpy(&v,&n,8);return v;}
static bool map_id(const char *s,uint32_t *out){
 if(!s||!*s||strlen(s)>10||(*s=='0'&&s[1]))return false;
 uint64_t n=0;for(;*s;s++){if(*s<'0'||*s>'9')return false;n=n*10+(unsigned)(*s-'0');if(n>INT32_MAX)return false;}*out=(uint32_t)n;return true;
}
static bool stable(const struct stat *a,const struct stat *b){return a->st_ino==b->st_ino&&a->st_size==b->st_size&&a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;}
static bool read_named_map(int dir,const char *name,uint32_t id,struct map *m){
 memset(m,0,sizeof(*m));
 int fd=openat(dir,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return false;
 struct stat before,after;bool ok=!fstat(fd,&before)&&S_ISREG(before.st_mode)&&before.st_size>=144&&before.st_size<=MAX_FILE;
 if(ok){m->size=(size_t)before.st_size;m->data=malloc(m->size);ok=m->data!=NULL;}
 size_t n=0;while(ok&&n<m->size){ssize_t got=read(fd,m->data+n,m->size-n);if(got<=0){ok=false;break;}n+=(size_t)got;}
 /* A second identical read also catches same-size writes on coarse clocks. */
 unsigned char check[4096];for(size_t off=0;ok&&off<m->size;off+=sizeof(check)){size_t part=m->size-off;if(part>sizeof(check))part=sizeof(check);if(pread(fd,check,part,(off_t)off)!=(ssize_t)part||memcmp(check,m->data+off,part))ok=false;}
 if(ok)ok=!fstat(fd,&after)&&stable(&before,&after);
 close(fd);
 if(!ok)goto fail;
 unsigned char *b=m->data;
 if(memcmp(b,"relo",4)||u32(b+4)!=7||u32(b+8)!=m->size||u32(b+20)!=id)goto fail;
 bool found=false;for(size_t i=24;i+GRID_HEADER<=m->size;i++)if(!memcmp(b+i,"GRID",4)){m->grid=i;found=true;break;}if(!found)goto fail;
 unsigned char *g=b+m->grid;m->w=u32(g+40);m->h=u32(g+44);m->packed=u32(g+80);uint32_t aux=u32(g+84);
 if(u32(g+4)!=2||!m->w||!m->h||m->w>2048||m->h>2048||(uint64_t)m->w*m->h>1000000||u32(g+76)!=m->w*m->h||!m->packed||(uint64_t)GRID_HEADER+m->packed+aux!=u32(g+8)||(uint64_t)m->grid+GRID_HEADER+m->packed+aux>m->size)goto fail;
 m->resolution=f64(g+16);if(!isfinite(m->resolution)||m->resolution<=0||m->resolution>1)goto fail;
 m->id=id;m->stamp=u32(b+12);uint64_t hash=UINT64_C(14695981039346656037);for(size_t i=0;i<m->size;i++){hash^=b[i];hash*=UINT64_C(1099511628211);}snprintf(m->revision,sizeof(m->revision),"%016"PRIx64,hash);return true;
fail:free(m->data);memset(m,0,sizeof(*m));return false;
}
static bool read_map(int dir,uint32_t id,struct map *m){char name[32];snprintf(name,sizeof(name),"MAP_%"PRIu32,id);return read_named_map(dir,name,id,m);}
static cJSON *metadata(const struct map *m){cJSON *o=cJSON_CreateObject();char id[16];snprintf(id,sizeof(id),"%"PRIu32,m->id);cJSON_AddStringToObject(o,"id",id);cJSON_AddStringToObject(o,"revision",m->revision);cJSON_AddNumberToObject(o,"saved_at",m->stamp);cJSON_AddNumberToObject(o,"bytes",m->size);cJSON_AddNumberToObject(o,"width",m->w);cJSON_AddNumberToObject(o,"height",m->h);cJSON_AddNumberToObject(o,"resolution_m",m->resolution);return o;}
static bool lz4(const unsigned char *src,size_t len,unsigned char *out,size_t cap){
 size_t i=0,n=0;while(i<len){unsigned token=src[i++];size_t lit=token>>4;
  if(lit==15){unsigned x;do{if(i==len)return false;x=src[i++];lit+=x;if(lit>cap)return false;}while(x==255);}
  if(lit>len-i||lit>cap-n)return false;
  memcpy(out+n,src+i,lit);i+=lit;n+=lit;if(i==len)return n==cap;
  if(len-i<2)return false;
  size_t off=src[i]|(unsigned)src[i+1]<<8;i+=2;if(!off||off>n)return false;
  size_t match=(token&15)+4;if((token&15)==15){unsigned x;do{if(i==len)return false;x=src[i++];match+=x;if(match>cap)return false;}while(x==255);}
  if(match>cap-n)return false;
  for(size_t j=0;j<match;j++){out[n]=out[n-off];n++;}
 }return n==cap;
}
static cJSON *preview(struct map *m){
 size_t cells=(size_t)m->w*m->h;unsigned char *raw=malloc(cells*2);if(!raw)return NULL;
 if(!lz4(m->data+m->grid+GRID_HEADER,m->packed,raw,cells*2)){free(raw);return NULL;}
 /* Bound response and canvas size. Average known occupancy within each tile. */
 unsigned step=(m->w>m->h?m->w:m->h)+399;step/=400;unsigned w=(m->w+step-1)/step,h=(m->h+step-1)/step;
 char *hex=malloc((size_t)w*h*2+1);if(!hex){free(raw);return NULL;}const char digits[]="0123456789abcdef";
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
  uint64_t sum=0;unsigned count=0;for(unsigned dy=0;dy<step&&y*step+dy<m->h;dy++)for(unsigned dx=0;dx<step&&x*step+dx<m->w;dx++){
   size_t at=((size_t)(y*step+dy)*m->w+x*step+dx)*2;unsigned v=raw[at]|(unsigned)raw[at+1]<<8;if(v>32767){free(hex);free(raw);return NULL;}if(v){sum+=v;count++;}
  }
  unsigned shade=count?1+(unsigned)((sum/count-1)*254/32766):0;size_t at=((size_t)y*w+x)*2;hex[at]=digits[shade>>4];hex[at+1]=digits[shade&15];
 }
 hex[(size_t)w*h*2]=0;free(raw);cJSON *o=metadata(m);cJSON_AddNumberToObject(o,"preview_width",w);cJSON_AddNumberToObject(o,"preview_height",h);
 /* GRID storage columns follow the native Y limit, rows the X limit.
    Keep the full-grid origin and integer sampling step: preview dimensions
    alone cannot recover the cropped control frame or the final partial tile. */
 double origin[2]={f64(m->data+m->grid+32)/m->resolution,f64(m->data+m->grid+24)/m->resolution};
 if(isfinite(origin[0])&&isfinite(origin[1]))cJSON_AddItemToObject(o,"grid_origin",cJSON_CreateDoubleArray(origin,2));
 cJSON_AddNumberToObject(o,"preview_step",step);
 cJSON_AddStringToObject(o,"encoding","occupancy-u8-hex");cJSON_AddStringToObject(o,"orientation","row0_top_v2");cJSON_AddStringToObject(o,"pixels",hex);cJSON_AddStringToObject(o,"source","saved_hoslam_grid");free(hex);return o;
}
static int compare(const void *a,const void *b){uint32_t x=*(const uint32_t*)a,y=*(const uint32_t*)b;return (x>y)-(x<y);}
#include "zigma_map_library.h"
#include "zigma_map_restore.h"
int main(int argc,char **argv){
 if(zigma_version_only(argc,argv,"zigma-maps-arm"))return 0;
 if(argc==3&&!strcmp(argv[1],"restore-context")){cJSON *o=restore_context(argv[2]);return o?print_json(o):lib_error("Native restore context unavailable");}
 if(argc==4&&!strcmp(argv[1],"restore-probe"))return restore_probe(argv[2],argv[3]);
 if(argc==4&&(!strcmp(argv[1],"restore-native")||!strcmp(argv[1],"restore-dry-run")))return native_restore_command(argv[2],argv[3],!strcmp(argv[1],"restore-dry-run"));
 if(argc==5&&!strcmp(argv[1],"library"))return library_command(argv[2],argv[3],argv[4]);
 if(argc==3&&!strcmp(argv[1],"active"))return native_active_command(argv[2]);
 if(argc==4&&(!strcmp(argv[1],"delete-native")||!strcmp(argv[1],"delete-dry-run")))return native_delete_command(argv[2],argv[3],!strcmp(argv[1],"delete-dry-run"),false);
 if(argc==4&&(!strcmp(argv[1],"clear-native")||!strcmp(argv[1],"clear-dry-run")))return native_delete_command(argv[2],argv[3],!strcmp(argv[1],"clear-dry-run"),true);
 if(argc<3||argc>4||(strcmp(argv[1],"list")&&strcmp(argv[1],"preview"))){fprintf(stderr,"Usage: zigma-maps list DIR | preview DIR ID\n");return 2;}
 bool list=!strcmp(argv[1],"list");uint32_t id=0;if((list&&argc!=3)||(!list&&(argc!=4||!map_id(argv[3],&id))))return 2;
 int dir=open(argv[2],O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(dir<0)return 1;cJSON *out=NULL;
 if(list){DIR *d=fdopendir(dup(dir));if(!d){close(dir);return 1;}uint32_t ids[MAX_MAPS];unsigned count=0,invalid=0;struct dirent *e;bool overflow=false;
  while((e=readdir(d)))if(!strncmp(e->d_name,"MAP_",4)&&map_id(e->d_name+4,&id)){if(count==MAX_MAPS){overflow=true;break;}ids[count++]=id;}
  closedir(d);if(overflow){close(dir);return 1;}qsort(ids,count,sizeof(*ids),compare);
  out=cJSON_CreateObject();cJSON *items=cJSON_AddArrayToObject(out,"items");size_t budget=0;
  for(unsigned i=0;i<count;i++){struct map m;if(!read_map(dir,ids[i],&m)){invalid++;continue;}budget+=m.size;if(budget>8u*1024u*1024u){free(m.data);cJSON_Delete(out);close(dir);return 1;}cJSON_AddItemToArray(items,metadata(&m));free(m.data);}
  cJSON_AddNumberToObject(out,"unreadable_count",invalid);cJSON_AddStringToObject(out,"source","local_saved_files");cJSON_AddBoolToObject(out,"selection_available",false);
 }else{struct map m;if(read_map(dir,id,&m)){out=preview(&m);free(m.data);}}
 close(dir);if(!out)return 1;char *wire=cJSON_PrintUnformatted(out);cJSON_Delete(out);if(!wire)return 1;puts(wire);free(wire);return 0;
}
