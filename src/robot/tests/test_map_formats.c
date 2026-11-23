/* Synthetic fixtures only; no vendor maps or robot access. */
#define main maps_program_main
#include "../src/zigma_maps.c"
#undef main
#include <assert.h>
static void fixture(unsigned char *b, unsigned version){
 memset(b,0,325);memcpy(b,"relo",4);put32(b+4,version);put32(b+8,325);put32(b+20,1);
 unsigned char *g=b+196;memcpy(g,"GRID",4);put32(g+4,2);put32(g+8,129);
 double resolution=0.05;memcpy(g+16,&resolution,8);put32(g+40,2);put32(g+44,2);put32(g+76,4);put32(g+80,9);
 /* One LZ4 literal block: unknown, free, midpoint, occupied. */
 memcpy(g+120,(unsigned char[]){0x80,0,0,1,0,0,64,255,127},9);
}
static void write_fixture(int dir,const unsigned char *b,size_t size){
 int fd=openat(dir,"MAP_1",O_WRONLY|O_CREAT|O_TRUNC,0600);assert(fd>=0);assert(write(fd,b,size)==(ssize_t)size);close(fd);
}
static void checksum2(unsigned char *b,size_t size){
 put32(b+8,size);put32(b+152,0);uint32_t sum=0;for(size_t i=0;i<size;i++)sum+=b[i];put32(b+152,sum);
}
static size_t fixture2(unsigned char *b,bool unpacked){
 memset(b,0,325);memcpy(b,"relo",4);put32(b+4,2);put32(b+20,1);
 put32(b+156,12);put32(b+172,16);
 size_t p=196; /* Grid2D starts after its length at 192. */
 b[p++]=10;b[p++]=35;b[p++]=9;double res=.05;memcpy(b+p,&res,8);p+=8;
 b[p++]=18;b[p++]=18;b[p++]=9;double max=2;memcpy(b+p,&max,8);p+=8;
 b[p++]=17;max=3;memcpy(b+p,&max,8);p+=8;
 b[p++]=26;b[p++]=4;b[p++]=8;b[p++]=2;b[p++]=16;b[p++]=2;
 unsigned char cells[]={0,1,128,128,1,255,255,1,0,255,1,2,3};
 if(!unpacked){b[p++]=18;b[p++]=sizeof cells;memcpy(b+p,cells,sizeof cells);p+=sizeof cells;}
 else {for(size_t i=0;i<sizeof cells;){b[p++]=16;do{b[p++]=cells[i];}while(cells[i++]&128);}}
 b[p++]=34;b[p++]=0; /* ProbabilityGrid */
 put32(b+192,p-196);put32(b+p,0);p+=4;checksum2(b,p);return p;
}
static void test_v2(int dir){
 unsigned char b[325];struct map m;
 for(unsigned unpacked=0;unpacked<2;unpacked++){
  size_t n=fixture2(b,unpacked);write_fixture(dir,b,n);assert(read_map(dir,1,&m));assert(m.protobuf);assert(restore_checksum(&m));assert(restore_format_supported(&robojet_map_profile,&m));assert(!restore_format_supported(&map_profiles[0],&m));
  cJSON *p=preview(&m);assert(p);assert(!strcmp(string(p,"pixels"),"000180ff"));
  cJSON *origin=item(p,"grid_origin");assert(fabs(cJSON_GetArrayItem(origin,0)->valuedouble-60)<1e-6);assert(fabs(cJSON_GetArrayItem(origin,1)->valuedouble-40)<1e-6);
  cJSON_Delete(p);free(m.data);
  /* Every truncation must fail, even with repaired outer size/checksum. */
  for(size_t len=160;len<n;len++){unsigned char shortmap[325];memcpy(shortmap,b,len);checksum2(shortmap,len);write_fixture(dir,shortmap,len);assert(!read_map(dir,1,&m));}
 }
 size_t n=fixture2(b,false);b[n-1]^=1;write_fixture(dir,b,n);assert(!read_map(dir,1,&m)); /* checksum */
 n=fixture2(b,false);put32(b+192,UINT32_MAX);checksum2(b,n);write_fixture(dir,b,n);assert(!read_map(dir,1,&m));
 n=fixture2(b,false);b[230]=3;checksum2(b,n);write_fixture(dir,b,n);assert(!read_map(dir,1,&m)); /* dimensions/count */
 n=fixture2(b,false);b[235]=128;checksum2(b,n);write_fixture(dir,b,n);assert(!read_map(dir,1,&m)); /* merged varint: missing cell */
 n=fixture2(b,false);b[n-6]=42;checksum2(b,n);write_fixture(dir,b,n);assert(!read_map(dir,1,&m)); /* TSDF */
 n=fixture2(b,false);b[240]=128;b[241]=128;b[242]=2;checksum2(b,n);write_fixture(dir,b,n);assert(!read_map(dir,1,&m)); /* occupancy 32768 */
 n=fixture2(b,false);b[244]=128;b[245]=2;checksum2(b,n);write_fixture(dir,b,n);assert(!read_map(dir,1,&m)); /* auxiliary 256 */
 /* Exercise parser boundaries without any allocations or native calls. */
 unsigned char bad[]={255,255,255,255,255,255,255,255,255,2};struct map_pb pb={bad,bad+sizeof bad};uint64_t v;assert(!map_varint(&pb,&v));
}
int main(void){
 char path[]="/tmp/zigma-map-formats-XXXXXX";assert(mkdtemp(path));int dir=open(path,O_RDONLY|O_DIRECTORY);assert(dir>=0);
 unsigned char b[325];struct map m;
 for(unsigned v=6;v<=7;v++){
  fixture(b,v);write_fixture(dir,b,sizeof b);assert(read_map(dir,1,&m));
  cJSON *p=preview(&m);assert(p);assert(!strcmp(string(p,"pixels"),"000180ff"));cJSON_Delete(p);free(m.data);
  write_fixture(dir,b,sizeof b-1);assert(!read_map(dir,1,&m));
  put32(b+20,2);write_fixture(dir,b,sizeof b);assert(!read_map(dir,1,&m));
  fixture(b,v);put32(b+196+4,3);write_fixture(dir,b,sizeof b);assert(!read_map(dir,1,&m));
  fixture(b,v);put32(b+196+76,5);write_fixture(dir,b,sizeof b);assert(!read_map(dir,1,&m));
  fixture(b,v);b[316]=0xf0;write_fixture(dir,b,sizeof b);assert(read_map(dir,1,&m));assert(!preview(&m));free(m.data);
 }
 for(unsigned v=5;v<=8;v+=3){fixture(b,v);write_fixture(dir,b,sizeof b);assert(!read_map(dir,1,&m));}
 test_v2(dir);
 unlinkat(dir,"MAP_1",0);close(dir);rmdir(path);puts("Map formats 2/6/7, previews and malformed input: PASS");return 0;
}
