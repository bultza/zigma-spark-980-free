#define main agent_program_main
#include "../src/zigma_agent.c"
#undef main
#include <assert.h>
int main(void){
 char folder[]="/tmp/report-history-XXXXXX";assert(mkdtemp(folder));snprintf(data_dir,sizeof data_dir,"%s",folder);
 cJSON *r=cJSON_Parse("{\"session_id\":1795598706,\"created\":1795601357,\"finish_at\":1795601352,\"text\":\"Cleaning finished\",\"clean_time_s\":1980,\"clean_area_m2\":34}");
 assert(report_archive(r));assert(report_archive(r));
 cJSON *cat=cJSON_CreateObject();cJSON_AddArrayToObject(cat,"items");sessions_local(cat);assert(cJSON_GetArraySize(get(cat,"items"))==1);
 cJSON *row=cJSON_GetArrayItem(get(cat,"items"),0);assert(num(row,"clean_time_s",0)==1980);assert(!strcmp(str(row,"source",""),"local_report"));assert(!cJSON_IsTrue(get(row,"map_available")));
 /* Exact native interval takes precedence; no duplicate row. */
 sessions_local(cat);assert(cJSON_GetArraySize(get(cat,"items"))==1);cJSON_Delete(cat);
 setbool(r,"partial_report",true);assert(report_archive(r));cat=cJSON_CreateObject();cJSON_AddArrayToObject(cat,"items");sessions_local(cat);row=cJSON_GetArrayItem(get(cat,"items"),0);assert(!strcmp(str(row,"outcome",""),"partial_reported"));cJSON_Delete(cat);cJSON_Delete(r);
 char file[1024];path(file,sizeof file,"cleaning-reports.json");unlink(file);rmdir(folder);puts("report history PASS: persistence, idempotency, native deduplication, partial status");return 0;
}
