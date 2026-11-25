/* Editable deployment label must not participate in component integrity. */
#define main agent_program_main
#include "../src/zigma_agent.c"
#undef main
#include <assert.h>
int main(void){
 char folder[]="/tmp/zigma-release-XXXXXX";assert(mkdtemp(folder));
 char manifest[512],label[512],component[512];
 snprintf(manifest,sizeof(manifest),"%s/release.json",folder);
 snprintf(label,sizeof(label),"%s/installer-version.txt",folder);
 snprintf(component,sizeof(component),"%s/component",folder);
 release_file=manifest;installer_version_file=label;
 FILE *f=fopen(component,"w");assert(f);fputs("component",f);fclose(f);
 char sha[65];assert(release_digest(component,sha));
 f=fopen(manifest,"w");assert(f);fprintf(f,"{\"items\":[{\"name\":\"test\",\"path\":\"%s\",\"sha256\":\"%s\",\"version\":\"V1\"}]}",component,sha);fclose(f);
 cJSON *o=release_view();assert(cJSON_IsNull(get(o,"installer_version")));cJSON_Delete(o);
 const char *values[]={"v1.1-rc20","manually changed <img src=x onerror=alert(1)>"};
 for(unsigned i=0;i<2;i++){
  f=fopen(label,"w");assert(f);fprintf(f,"%s\n",values[i]);fclose(f);
  o=release_view();assert(!strcmp(str(o,"installer_version",""),values[i]));
  cJSON *items=get(o,"items");assert(cJSON_GetArraySize(items)==1);
  assert(!strcmp(str(cJSON_GetArrayItem(items,0),"status",""),"verified"));cJSON_Delete(o);
 }
 unlink(label);unlink(manifest);unlink(component);rmdir(folder);
 puts("PASS: missing/editable installer label is independent of verified components");return 0;
}
