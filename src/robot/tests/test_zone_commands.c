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
