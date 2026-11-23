#define msgsnd zone_test_msgsnd
#define main zones_cli_main
#include "../src/zigma_zones.c"
#undef main
#include <assert.h>
static int sends;static long types[2];static unsigned char sent[2][4096];
int zone_test_msgsnd(int q,const void *msg,size_t size,int flags){assert(q==123&&size==4096&&flags==IPC_NOWAIT&&sends<2);types[sends]=*(const long*)msg;memcpy(sent[sends++],(const unsigned char*)msg+sizeof(long),4096);return 0;}
static uint32_t word(unsigned char *p){uint32_t n;memcpy(&n,p,4);return n;}
static float real(unsigned char *p){float n;memcpy(&n,p,4);return n;}
int main(void){
 assert(zones_profile_hash("8e323cfe110e4aa78d680b34c6c26a4276ada7869d58f49de64c0440fa551f6c")==3);
 assert(zones_navigation_hash("1b2751efd2a4d5afc208fce099401d90c581f0cec41cfe440e9a39d519ece664"));assert(!zones_navigation_hash("unknown"));

 assert(zones_refresh_pair("8e323cfe110e4aa78d680b34c6c26a4276ada7869d58f49de64c0440fa551f6c","f793e93abb85a1a7b1cbe8cec1f53b2c6d9aac7c21c8fb3b956f460fe693f88c"));
 assert(!zones_refresh_pair("8e323cfe110e4aa78d680b34c6c26a4276ada7869d58f49de64c0440fa551f6c","b10fc7b48d1bdb990a2c37b82f7d45fa4fd7358f428f42d58ae885aca2970255"));
 assert(!zones_refresh_pair("4cf0709919f0e894a5d3ed1c60ca9d6bf11d7c47120077e23f6c13902c5d9a9a","f793e93abb85a1a7b1cbe8cec1f53b2c6d9aac7c21c8fb3b956f460fe693f88c"));
 unsigned char message[4096];
 /* Expected bytes follow native type6 receiver: length/count/type/x/-y.
  * Unequal signed coordinates expose axis swaps and incorrect scaling. */
 cJSON *a=cJSON_Parse("[{\"x\":[-4,-4,-20,-20],\"y\":[43,73,73,43],\"can_clean\":false,\"can_mop\":false},{\"x\":[28,28,-8,-8],\"y\":[50,81,81,50],\"can_clean\":true,\"can_mop\":false}]");
 zones_native_message(a,message);
 assert(!zones_send(2,123,a,"unused"));assert(sends==2&&types[0]==6&&types[1]==16&&!memcmp(sent[0],message,4096));for(int j=0;j<4096;j++)assert(!sent[1][j]);
 sends=0;const char *legacy="{\"ProtocolVersion\":\"1.0\",\"forbidden_zone_info\":[]}";assert(!zones_send(1,123,a,legacy));assert(sends==1&&types[0]==1&&!strcmp((char*)sent[0],legacy));sends=0;assert(zones_send(0,123,a,legacy)&&!sends);
 assert(word(message)==80&&word(message+4)==2&&word(message+8)==3&&word(message+44)==1);
 assert(real(message+12)==-4&&real(message+16)==-43&&real(message+20)==-4&&real(message+24)==-73);
 assert(real(message+48)==28&&real(message+52)==-50&&real(message+72)==-8&&real(message+76)==-50);
 for(int i=80;i<4096;i++)assert(message[i]==0);
 /* Edit only the second zone, preserving the first native polygon. */
 unsigned char first[36];memcpy(first,message+8,36);
 cJSON *z=cJSON_GetArrayItem(a,1);cJSON_ReplaceItemInArray(cJSON_GetObjectItemCaseSensitive(z,"x"),0,cJSON_CreateNumber(30));
 cJSON_ReplaceItemInObjectCaseSensitive(z,"can_clean",cJSON_CreateFalse());cJSON_ReplaceItemInObjectCaseSensitive(z,"can_mop",cJSON_CreateTrue());
 zones_native_message(a,message);assert(!memcmp(first,message+8,36)&&word(message+44)==2&&real(message+48)==30);
 cJSON_ReplaceItemInObjectCaseSensitive(z,"can_clean",cJSON_CreateTrue());zones_native_message(a,message);assert(word(message+44)==0&&!memcmp(first,message+8,36));
 cJSON_DeleteItemFromArray(a,1);zones_native_message(a,message);assert(word(message)==44&&word(message+4)==1&&!memcmp(first,message+8,36));
 cJSON_DeleteItemFromArray(a,0);zones_native_message(a,message);assert(word(message)==8&&word(message+4)==0);
 for(int i=8;i<4096;i++)assert(message[i]==0);
 /* RoboJet must send flat legacy JSON, never a 36-byte native record.
  * Empty array deletes polygons only: wall keys must remain absent. */
 sends=0;assert(!zones_send(3,123,a,"unused")&&sends==1&&types[0]==1);
 cJSON *legacy_json=cJSON_Parse((char*)sent[0]);assert(legacy_json);
 assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(legacy_json,"forbidden_zone_x"))==0);
 assert(!cJSON_GetObjectItemCaseSensitive(legacy_json,"laser_wall_line_x"));cJSON_Delete(legacy_json);
 cJSON *quad=cJSON_Parse("{\"x\":[-4,-4,20,20],\"y\":[43,73,73,43],\"can_clean\":false,\"can_mop\":false}");cJSON_AddItemToArray(a,quad);
 sends=0;assert(!zones_send(3,123,a,"unused"));legacy_json=cJSON_Parse((char*)sent[0]);
 assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(legacy_json,"forbidden_zone_x"))==4);
 assert(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(legacy_json,"forbidden_zone_y"),0)->valueint==43); /* App negates Y exactly once. */
 assert(!cJSON_GetObjectItemCaseSensitive(legacy_json,"forbidden_zone_info"));cJSON_Delete(legacy_json);
 cJSON_ReplaceItemInObjectCaseSensitive(quad,"can_mop",cJSON_CreateTrue());sends=0;assert(zones_send(3,123,a,"unused")&&sends==0);
 cJSON_Delete(a);
 assert(zones_profile_hash("784fbcda505db72675bf01a82f5b02f6eca8cb313a7916176ed7bbf10d7dd912")==2);
 assert(zones_profile_hash("4cf0709919f0e894a5d3ed1c60ca9d6bf11d7c47120077e23f6c13902c5d9a9a")==1);
 assert(!zones_profile_hash("unknown"));
 assert(zones_navigation_hash("b10fc7b48d1bdb990a2c37b82f7d45fa4fd7358f428f42d58ae885aca2970255"));
 assert(zones_navigation_hash("6b7b11091132e75d9787abd59a8ac4f758da3732cccf86dc5702e99bd772a5d8"));
 assert(!zones_navigation_hash("6b7b11091132e75d9787abd59a8ac4f758da3732cccf86dc5702e99bd772a5d9"));
 assert(!zones_navigation_hash("unknown"));
 puts("Zone native edit/delete/empty framing, signed grid coordinates, restrictions and exact profile selection: PASS");return 0;
}
