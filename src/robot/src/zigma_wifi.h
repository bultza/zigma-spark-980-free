/* Passive WLAN signal read: no scan, subprocess or vendor IPC. */
static bool wifi_signal_read(const char *wireless, const char *carrier, int *dbm){
 FILE *f=fopen(carrier,"re");int connected=0;
 if(!f)return false;
 int ok=fscanf(f,"%d",&connected);fclose(f);
 if(ok!=1||connected!=1)return false;
 f=fopen(wireless,"re");if(!f)return false;
 char line[512],iface[32];unsigned flags;double quality,level;bool found=false;
 while(fgets(line,sizeof line,f)){
  if(sscanf(line," %31[^:]: %x %lf %lf",iface,&flags,&quality,&level)==4&&
     !strcmp(iface,"wlan0")&&isfinite(level)&&level>=-127&&level<0){
   *dbm=(int)level;found=true;break;
  }
 }
 fclose(f);return found;
}
static void wifi_status(cJSON *o){
 int dbm;cJSON *w=cJSON_CreateObject();
 bool available=strcmp(mode,"demo")&&wifi_signal_read("/proc/net/wireless","/sys/class/net/wlan0/carrier",&dbm);
 cJSON_AddBoolToObject(w,"available",available);
 cJSON_AddStringToObject(w,"interface","wlan0");
 if(available){cJSON_AddNumberToObject(w,"signal_dbm",dbm);cJSON_AddNumberToObject(w,"observed_at",time(NULL));}
 cJSON_DeleteItemFromObjectCaseSensitive(o,"wifi");cJSON_AddItemToObject(o,"wifi",w);
}
