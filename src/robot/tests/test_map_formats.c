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
 unlinkat(dir,"MAP_1",0);close(dir);rmdir(path);puts("Map formats 6/7, previews and malformed input: PASS");return 0;
}
