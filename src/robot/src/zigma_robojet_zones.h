/* RoboJet 8e323cfe: recv_packet + native map-config serializer agree on
 * count+16*n walls and count+32*n unclassified quadrilaterals. Walls remain read-only; polygons block all cleaning. */
struct robojet_zone_snapshot {uint32_t pointer,count;unsigned char bytes[4+64*32];size_t size;};
static bool robojet_zone_read(int fd,uint32_t slot,size_t stride,struct robojet_zone_snapshot *s){
 uint32_t again=0;
 if(!memory_read(fd,slot,&s->pointer,4)||!s->pointer||!memory_read(fd,s->pointer,&s->count,4)||s->count>64)return false;
 s->size=4+s->count*stride;
 return memory_read(fd,s->pointer,s->bytes,s->size)&&!memcmp(s->bytes,&s->count,4)&&memory_read(fd,slot,&again,4)&&again==s->pointer;
}
static bool cached_robojet_zones(cJSON *map,int fd){
 uint32_t initialized=0;if(!memory_read(fd,0x43e08,&initialized,4)||initialized!=1)return false;
 cJSON *origin=get(map,"map_origin"),*ox=cJSON_GetArrayItem(origin,0),*oy=cJSON_GetArrayItem(origin,1);
 if(cJSON_GetArraySize(origin)!=2||!cJSON_IsNumber(ox)||!cJSON_IsNumber(oy)||!isfinite(ox->valuedouble)||!isfinite(oy->valuedouble))return false;
 struct robojet_zone_snapshot first[2],second[2];const uint32_t slots[]={0x43e24,0x43e28};const size_t strides[]={16,32};
 /* Compare the whole pair before publishing anything, never a partial set. */
 for(int i=0;i<2;i++)if(!robojet_zone_read(fd,slots[i],strides[i],&first[i]))return false;
 for(int i=0;i<2;i++)if(!robojet_zone_read(fd,slots[i],strides[i],&second[i])||first[i].pointer!=second[i].pointer||first[i].size!=second[i].size||memcmp(first[i].bytes,second[i].bytes,first[i].size))return false;
 if(!memory_read(fd,0x43e08,&initialized,4)||initialized!=1)return false;
 cJSON *zones=cJSON_CreateArray();
 for(int kind=0;kind<2;kind++)for(uint32_t i=0;i<first[kind].count;i++){
  cJSON *zone=cJSON_CreateObject(),*points=cJSON_CreateArray();cJSON_AddItemToObject(zone,"points",points);cJSON_AddItemToArray(zones,zone);
  for(size_t j=0;j<strides[kind]/8;j++){
   float x,y;memcpy(&x,first[kind].bytes+4+i*strides[kind]+j*8,4);memcpy(&y,first[kind].bytes+8+i*strides[kind]+j*8,4);
   if(!isfinite(x)||!isfinite(y)||fabs(x)>20000||fabs(y)>20000){cJSON_Delete(zones);return false;}
   cJSON *p=cJSON_CreateObject();cJSON_AddNumberToObject(p,"x",x+ox->valuedouble);cJSON_AddNumberToObject(p,"y",-y+oy->valuedouble);cJSON_AddItemToArray(points,p);
  }
  cJSON_AddStringToObject(zone,"kind",kind?"forbidden_polygon":"virtual_wall");cJSON_AddNumberToObject(zone,"id",cJSON_GetArraySize(zones));
  if(kind){cJSON_AddBoolToObject(zone,"can_clean",false);cJSON_AddBoolToObject(zone,"can_mop",false);}else{cJSON_AddNullToObject(zone,"can_clean");cJSON_AddNullToObject(zone,"can_mop");}cJSON_AddBoolToObject(zone,"read_only",!kind);cJSON_AddStringToObject(zone,"source","robojet_legacy_cache");
 }
 replace_value(map,"zones",zones);replace_value(map,"zones_available",cJSON_CreateBool(true));replace_value(map,"zones_read_only",cJSON_CreateBool(false));replace_value(map,"zones_read_at",cJSON_CreateNumber(time(NULL)));return true;
}
