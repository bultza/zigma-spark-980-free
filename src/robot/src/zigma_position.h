/* Passive position selection. Receipt/read time is never a sensor timestamp.
 * Keep navigation_pose independent: recorded map points cannot prove arrival. */
#ifndef ZIGMA_POSITION_H
#define ZIGMA_POSITION_H
#include "cJSON.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>
static const cJSON *pos_get(const cJSON *j,const char *k){return cJSON_GetObjectItemCaseSensitive(j,k);}
static double pos_num(const cJSON *j,const char *k){const cJSON *v=pos_get(j,k);return cJSON_IsNumber(v)?v->valuedouble:NAN;}
static const char *pos_str(const cJSON *j,const char *k){const cJSON *v=pos_get(j,k);return cJSON_IsString(v)?v->valuestring:"";}
static bool pos_recent(double stamp,double now,double limit){return isfinite(stamp)&&stamp>0&&isfinite(now)&&now>=stamp&&now-stamp<=limit;}
static bool pos_geometry(const cJSON *m){
 const cJSON *o=pos_get(m,"map_origin"),*x=cJSON_GetArrayItem(o,0),*y=cJSON_GetArrayItem(o,1);
 double w=pos_num(m,"width"),h=pos_num(m,"height");
 return *pos_str(m,"map_id")&&isfinite(w)&&isfinite(h)&&w>0&&h>0&&w<=2048&&h<=2048&&floor(w)==w&&floor(h)==h&&cJSON_GetArraySize(o)==2&&cJSON_IsNumber(x)&&cJSON_IsNumber(y)&&isfinite(x->valuedouble)&&isfinite(y->valuedouble);
}
static bool pos_xy(const cJSON *p,const cJSON *m){double x=pos_num(p,"x"),y=pos_num(p,"y");return isfinite(x)&&isfinite(y)&&x>=0&&y>=0&&x<pos_num(m,"width")&&y<pos_num(m,"height");}
static bool pos_same(const cJSON *p,const cJSON *m){
 const char *keys[]={"map_id","width","height","map_origin",NULL};
 for(int i=0;keys[i];i++)if(!pos_get(p,keys[i])||!pos_get(m,keys[i])||!cJSON_Compare(pos_get(p,keys[i]),pos_get(m,keys[i]),1))return false;
 return true;
}
static cJSON *position_select(const cJSON *status,const cJSON *map,double now){
 const cJSON *p=pos_get(status,"navigation_pose");const char *source="unavailable",*quality="unavailable";double stamp=NAN,heading=NAN,res=NAN;
 bool blocked=cJSON_IsTrue(pos_get(map,"reset_pending"))||cJSON_IsObject(pos_get(map,"reset_pending"))||cJSON_IsTrue(pos_get(status,"map_reset_pending"))||cJSON_IsObject(pos_get(status,"map_reset_pending"))||!strcmp(pos_str(status,"working_status"),"Relocation");
 bool geometry=!blocked&&pos_geometry(map);
 if(geometry&&pos_same(p,map)&&pos_xy(p,map)&&!strcmp(pos_str(p,"kind"),"navigation_pose_cache")&&!strcmp(pos_str(p,"frame"),"map_grid")&&pos_recent(pos_num(p,"cache_read_at"),now,15)&&isfinite(pos_num(p,"heading"))&&fabs(pos_num(p,"heading"))<=3.141593&&pos_num(p,"resolution_m")>=0.001&&pos_num(p,"resolution_m")<=1){
  source="navigation_memory";quality="cached";stamp=pos_num(p,"cache_read_at");heading=pos_num(p,"heading");res=pos_num(p,"resolution_m");
 }else{
  p=NULL;
  if(geometry&&cJSON_IsTrue(pos_get(map,"available"))){
   const cJSON *reported=pos_get(map,"position"),*estimate=pos_get(map,"position_estimate"),*endpoint=pos_get(map,"path_endpoint");
   if(pos_xy(reported,map)){p=reported;source="map_position";quality="reported";stamp=pos_num(map,"snapshot_read_at");}
   else if(pos_xy(estimate,map)&&!strcmp(pos_str(estimate,"kind"),"recent_path_estimate")&&pos_recent(pos_num(estimate,"received_at"),now,6)){p=estimate;source="map_path";quality="estimated";stamp=pos_num(estimate,"received_at");}
   else if(pos_xy(endpoint,map)&&!strcmp(pos_str(endpoint,"kind"),"recorded_path_endpoint")){p=endpoint;source="map_path";quality="recorded";stamp=pos_num(map,"path_changed_at");}
  }
 }
 cJSON *o=cJSON_CreateObject();bool ok=p!=NULL;
 cJSON_AddBoolToObject(o,"available",ok);cJSON_AddBoolToObject(o,"fresh",false);cJSON_AddStringToObject(o,"source",source);cJSON_AddStringToObject(o,"quality",quality);
 cJSON_AddStringToObject(o,"frame","map_grid");cJSON_AddStringToObject(o,"units","cells");cJSON_AddStringToObject(o,"heading_units","radians");
 const char *keys[]={"map_id","width","height","map_origin",NULL};for(int i=0;keys[i];i++)cJSON_AddItemToObject(o,keys[i],ok?cJSON_Duplicate(pos_get(map,keys[i]),1):cJSON_CreateNull());
 cJSON_AddItemToObject(o,"x",ok?cJSON_CreateNumber(pos_num(p,"x")):cJSON_CreateNull());cJSON_AddItemToObject(o,"y",ok?cJSON_CreateNumber(pos_num(p,"y")):cJSON_CreateNull());
 cJSON_AddItemToObject(o,"heading",isfinite(heading)?cJSON_CreateNumber(heading):cJSON_CreateNull());cJSON_AddItemToObject(o,"resolution_m",isfinite(res)?cJSON_CreateNumber(res):cJSON_CreateNull());
 bool timed=isfinite(stamp)&&stamp>0&&stamp<=now;
 cJSON_AddItemToObject(o,"cache_read_at",!strcmp(source,"navigation_memory")?cJSON_CreateNumber(stamp):cJSON_CreateNull());
 cJSON_AddItemToObject(o,"received_at",timed?cJSON_CreateNumber(stamp):cJSON_CreateNull());cJSON_AddItemToObject(o,"age_s",timed?cJSON_CreateNumber(now-stamp):cJSON_CreateNull());cJSON_AddNullToObject(o,"measured_at");return o;
}
#endif
