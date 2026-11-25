#define main agent_program_main
#include "../src/zigma_agent.c"
#undef main
#include <assert.h>
int main(void){
 char folder[]="/tmp/web-fan-XXXXXX";assert(mkdtemp(folder));snprintf(data_dir,sizeof data_dir,"%s",folder);
 char helper[512],log[512];snprintf(helper,sizeof helper,"%s/control",folder);snprintf(log,sizeof log,"%s/calls",folder);
 FILE *f=fopen(helper,"w");assert(f);fprintf(f,"#!/bin/sh\nprintf '%%s %%s\\n' \"$1\" \"$2\" >> '%s'\n",log);fclose(f);chmod(helper,0700);
 mode="commands";control_bin=helper;state=cJSON_CreateObject();cJSON_AddArrayToObject(state,"events");char error[200];
 for(int i=1;i<=3;i++){cJSON *body=cJSON_CreateObject();cJSON_AddNumberToObject(body,"fan_power",i);assert(issue("setfan",body,error,sizeof error));cJSON_Delete(body);}
 const char *bad[]={"{}","{\"fan_power\":0}","{\"fan_power\":4}","{\"fan_power\":1.5}","{\"fan_power\":\"2\"}"};
 for(unsigned i=0;i<sizeof bad/sizeof *bad;i++){cJSON *body=cJSON_Parse(bad[i]);assert(!issue("setfan",body,error,sizeof error));cJSON_Delete(body);}
 char b[100]={0};f=fopen(log,"r");assert(f);fread(b,1,sizeof b-1,f);fclose(f);assert(!strcmp(b,"setfan 1\nsetfan 2\nsetfan 3\n"));
 f=fopen(helper,"w");fputs("#!/bin/sh\nexit 1\n",f);fclose(f);cJSON *body=cJSON_Parse("{\"fan_power\":2}");assert(!issue("setfan",body,error,sizeof error));cJSON_Delete(body);
 cJSON_Delete(state);unlink(helper);unlink(log);rmdir(folder);puts("web fan dispatch PASS: only setfan, strict validation, failures propagate");return 0;
}
