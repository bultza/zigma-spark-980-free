/* Synthetic process memory; no robot processes or IPC. */
#define main maps_program_main
#include "../src/zigma_maps.c"
#undef main
#include <assert.h>
static void word(int fd,off_t at,uint32_t n){unsigned char b[4];put32(b,n);assert(pwrite(fd,b,4,at)==4);}
static cJSON *request_dry(const char *request,bool clear){
 FILE *out=tmpfile();assert(out);fflush(stdout);int saved=dup(STDOUT_FILENO);assert(saved>=0);assert(dup2(fileno(out),STDOUT_FILENO)>=0);
 assert(native_delete_command("146",request,true,clear)==0);fflush(stdout);assert(dup2(saved,STDOUT_FILENO)>=0);close(saved);rewind(out);
 char b[2048]={0};assert(fread(b,1,sizeof(b)-1,out)>0);fclose(out);cJSON *o=cJSON_Parse(b);assert(o);return o;
}
static cJSON *deletion(const char *request){return request_dry(request,false);}
int main(void){
 char file[]="/tmp/zigma-map-native-XXXXXX";int fd=mkstemp(file);assert(fd>=0);unlink(file);
 for(size_t i=0;i<sizeof(map_profiles)/sizeof(*map_profiles);i++){
  assert(ftruncate(fd,0)==0);const struct map_native_profile *p=&map_profiles[i];
  unsigned char code[12]={1,0x4b,0x98,0x6c,0x70,0x47,0,0xbf};put32(code+8,p->base);
  assert(pwrite(fd,code,12,p->getter)==12);word(fd,p->initialized,1);word(fd,p->base+0x48,18);
  unsigned char state[100]={0};assert(pwrite(fd,state,p->status_size,p->status)==(ssize_t)p->status_size);
  assert(p->clear_supported);assert(p->restore_supported==!strcmp(p->name,"zigma"));
  assert(map_profile_fd(fd)==p);uint32_t id=0;assert(map_identity_fd(fd,&id)&&id==18);
  unsigned idle[]={1,2,4,30,31};for(size_t j=0;j<5;j++){word(fd,p->status,idle[j]);assert(map_idle_fd(fd));}
  word(fd,p->status,11);assert(!map_idle_fd(fd));word(fd,p->status,31);word(fd,p->status+44,23);assert(!map_idle_fd(fd));word(fd,p->status+44,0);
  word(fd,p->initialized,0);assert(!map_identity_fd(fd,&id)&&!map_idle_fd(fd));word(fd,p->initialized,1);
  word(fd,p->base+0x48,0xffffffff);assert(!map_identity_fd(fd,&id));word(fd,p->base+0x48,0);assert(map_identity_fd(fd,&id)&&id==0);
  code[0]^=1;assert(pwrite(fd,code,12,p->getter)==12);assert(!map_profile_fd(fd)&&!map_identity_fd(fd,&id)&&!map_idle_fd(fd));
 }
 assert(ftruncate(fd,0)==0);
 unsigned char ng[]={0x68,0x6c,2,0xb0,0x70,0xbd},ns[]={0x70,0xb5,0x14,0x4c,0x14,0x4b,0x7c,0x44,0xe5,0x58,0x2b,0x78,0xbf,0xf3,0x5b,0x8f,0xdb,7,3,0xd5,0x11,0x4b,0xe6,0x58,0x30,0x46};
 assert(pwrite(fd,ng,sizeof ng,0x13fba6)==sizeof ng);assert(pwrite(fd,ns,sizeof ns,0x5c474)==sizeof ns);
 word(fd,0x3a5a24,0x3c26d0);word(fd,0x3a5d80,0x3c26d4);word(fd,0x3c26d0,1);word(fd,0x3c2718,21);uint32_t nid;
 assert(puron_navigation_identity_fd(fd,&nid)&&nid==21);word(fd,0x3c2718,0);assert(puron_navigation_identity_fd(fd,&nid)&&nid==0);
 word(fd,0x3c26d0,0);assert(!puron_navigation_identity_fd(fd,&nid));word(fd,0x3c26d0,1);word(fd,0x3a5d80,0);assert(!puron_navigation_identity_fd(fd,&nid));
 assert(ftruncate(fd,0)==0);assert(!map_identity_fd(fd,&(uint32_t){0})&&!map_idle_fd(fd));close(fd);
 cJSON *o=deletion("{\"id\":\"18\",\"active_id\":\"18\",\"confirm\":true}");assert(item(o,"error")&&!item(o,"cmd"));cJSON_Delete(o);
 o=deletion("{\"id\":\"16\",\"active_id\":\"18\",\"confirm\":false}");assert(item(o,"error"));cJSON_Delete(o);
 o=deletion("{\"id\":\"16\",\"active_id\":\"18\",\"confirm\":true}");assert(!strcmp(string(o,"cmd"),"del_local_map"));cJSON *ids=item(o,"DelHismapIds");assert(cJSON_GetArraySize(ids)==1&&!strcmp(cJSON_GetArrayItem(ids,0)->valuestring,"16"));cJSON_Delete(o);
 o=request_dry("{\"id\":\"21\",\"active_id\":\"21\",\"confirm\":true,\"confirmation_text\":\"CLEAR\"}",true);assert(!strcmp(string(o,"WorkStatus"),"ClearMap")&&!item(o,"DelHismapIds")&&!item(o,"cmd"));cJSON_Delete(o);
 o=request_dry("{\"id\":\"18\",\"active_id\":\"21\",\"confirm\":true,\"confirmation_text\":\"CLEAR\"}",true);assert(item(o,"error"));cJSON_Delete(o);
 o=request_dry("{\"id\":\"21\",\"active_id\":\"21\",\"confirm\":true}",true);assert(item(o,"error"));cJSON_Delete(o);
 unsigned char payload[4096];assert(!puron_delete_message(0,21,payload));assert(!puron_delete_message(21,21,payload));assert(puron_delete_message(16,0,payload));assert(u32(payload+4)==16);assert(!puron_delete_message(0xffffffff,21,payload));assert(puron_delete_message(16,21,payload));assert(u32(payload)==8&&u32(payload+4)==16);for(size_t i=8;i<sizeof payload;i++)assert(payload[i]==0);
 /* Match the real ARM receiver: r5 points to mtype, ID is at r5+8. */
 unsigned char received[4100]={0};put32(received,27);memcpy(received+4,payload,4096);assert(u32(received)==27&&u32(received+4)==8&&u32(received+8)==16);
 assert(u32(received+8)!=0&&u32(received+8)!=21);
 assert(puron_clear_message(21,21,payload));assert(u32(payload)==8&&u32(payload+4)==21);
 assert(!puron_clear_message(0,0,payload));assert(!puron_clear_message(18,21,payload));assert(!puron_delete_message(21,21,payload));
 puts("Zigma/Puron map identity, state, errors and unsupported memory: PASS");return 0;
}
