#ifndef ZIGMA_REPORT_MAP_H
#define ZIGMA_REPORT_MAP_H
/* Pure historical adapter using the established observer grid/path contract. */
static int zr_number(const cJSON *o,const char *k){const cJSON *v=zs_get(o,k);return cJSON_IsNumber(v)?v->valueint:0;}
static unsigned char *zr_decode(const cJSON *v,size_t *size){
 if(!cJSON_IsString(v))return NULL;
 static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
 const char *s=v->valuestring;size_t len=strlen(s);if(len%4||len>1000000)return NULL;
 unsigned char *out=malloc(len/4*3+1);if(!out)return NULL;size_t n=0;
 for(size_t i=0;i<len;i+=4){unsigned a[4];for(int j=0;j<4;j++){const char *p=strchr(alphabet,s[i+j]);
   if(s[i+j]=='=')a[j]=0;else if(s[i+j]&&p)a[j]=(unsigned)(p-alphabet);else{free(out);return NULL;}}
  if(s[i]=='='||s[i+1]=='='||(s[i+2]=='='&&s[i+3]!='=')||((s[i+2]=='='||s[i+3]=='=')&&i+4!=len)){free(out);return NULL;}
  out[n++]=(unsigned char)((a[0]<<2)|(a[1]>>4));if(s[i+2]!='=')out[n++]=(unsigned char)((a[1]<<4)|(a[2]>>2));if(s[i+3]!='=')out[n++]=(unsigned char)((a[2]<<6)|a[3]);
 }*size=n;return out;
}
static void zr_point(cJSON *o,const char *key,double x,double y){cJSON *p=cJSON_CreateObject();cJSON_AddNumberToObject(p,"x",x);cJSON_AddNumberToObject(p,"y",y);cJSON_AddItemToObject(o,key,p);}
static cJSON *zr_map(cJSON *raw){
 int w=zr_number(raw,"MapWidth"),h=zr_number(raw,"MapHigh");const cJSON *origin=zs_get(raw,"MapOrigin");
 if(w<=0||h<=0||w>2048||h>2048||w*h>1000000||cJSON_GetArraySize(origin)!=2)return NULL;
 cJSON *ox=cJSON_GetArrayItem(origin,0),*oy=cJSON_GetArrayItem(origin,1);if(!cJSON_IsNumber(ox)||!cJSON_IsNumber(oy)||!isfinite(ox->valuedouble)||!isfinite(oy->valuedouble))return NULL;
 size_t len;unsigned char *data=zr_decode(zs_get(raw,"MapData"),&len);if(!data||len!=((size_t)w*h+3)/4){free(data);return NULL;}
 cJSON *o=cJSON_CreateObject(),*cells=cJSON_CreateArray();for(int i=0;i<w*h;i++)cJSON_AddItemToArray(cells,cJSON_CreateNumber((data[i/4]>>(6-2*(i%4)))&3));free(data);
 cJSON_AddItemToObject(o,"map_origin",cJSON_Duplicate(origin,1));cJSON_AddItemToObject(o,"cells",cells);cJSON_AddNumberToObject(o,"width",w);cJSON_AddNumberToObject(o,"height",h);cJSON_AddBoolToObject(o,"available",true);
 cJSON_AddStringToObject(o,"source","local_cleaning_record");cJSON_AddStringToObject(o,"freshness","unknown");cJSON_AddNullToObject(o,"measurement_time");
 cJSON_AddStringToObject(o,"warning","Historical cleaning map. Recorded path is not a live position.");
 const cJSON *id=zs_get(raw,"map_id");if(cJSON_IsString(id))cJSON_AddStringToObject(o,"map_id",id->valuestring);
 cJSON_AddItemToObject(o,"zones",cJSON_CreateArray());const cJSON *dock=zs_get(raw,"ChargerPoint"),*dx=cJSON_GetArrayItem(dock,0),*dy=cJSON_GetArrayItem(dock,1);
 if(cJSON_IsNumber(dx)&&cJSON_IsNumber(dy))zr_point(o,"dock",dx->valuedouble+ox->valuedouble,dy->valuedouble+oy->valuedouble);
 size_t bytes=0,types_len=0;unsigned char *pts=zr_decode(zs_get(raw,"PointData"),&bytes),*types=zr_decode(zs_get(raw,"PointType"),&types_len);cJSON *path=cJSON_CreateArray();
 if(pts&&types&&bytes%4==0&&types_len==(bytes/4+3)/4)for(size_t i=0;i<bytes;i+=4){int x=(short)((unsigned)pts[i]|((unsigned)pts[i+1]<<8)),y=(short)((unsigned)pts[i+2]|((unsigned)pts[i+3]<<8));
  if(bytes==4&&x==0&&y==0)continue;
  double px=x/10.0+ox->valuedouble,py=y/10.0+oy->valuedouble;if(px<0||px>=w||py<0||py>=h)continue;
  cJSON *p=cJSON_CreateObject();cJSON_AddNumberToObject(p,"x",px);cJSON_AddNumberToObject(p,"y",py);cJSON_AddItemToArray(path,p);
 }
 free(pts);free(types);if(cJSON_GetArraySize(path)){cJSON *last=cJSON_GetArrayItem(path,cJSON_GetArraySize(path)-1);cJSON *endpoint=cJSON_Duplicate(last,1);cJSON_AddStringToObject(endpoint,"kind","recorded_path_endpoint");cJSON_AddBoolToObject(endpoint,"live",false);cJSON_AddItemToObject(o,"path_endpoint",endpoint);}
 cJSON_AddItemToObject(o,"path",path);
 cJSON_AddStringToObject(o,"map_role","historical");return o;
}
#endif
