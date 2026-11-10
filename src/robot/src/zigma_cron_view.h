/* Read-only view of the actual root table, including entries we do not own. */
static char *read_cron_table(char *error,size_t cap){
 if(!cron_dir){snprintf(error,cap,"Cron is not connected on this instance");return NULL;}
 char file[1024];snprintf(file,sizeof(file),"%s/root",cron_dir);struct stat st;
 if(lstat(file,&st)){if(errno==ENOENT)return strdup("");snprintf(error,cap,"Cannot inspect the robot crontab");return NULL;}
 if(!S_ISREG(st.st_mode)||st.st_size>LIMIT-128){snprintf(error,cap,"Robot crontab is not a supported regular file or is too large");return NULL;}
 char *raw=calloc(LIMIT,1);if(!raw){snprintf(error,cap,"Cannot allocate crontab view");return NULL;}
 char *args[]={(char*)cron_bin,"-c",(char*)cron_dir,"-l",NULL};
 if(run_process(args,raw,LIMIT)||strlen(raw)>=LIMIT-1){free(raw);snprintf(error,cap,"Cannot read the complete robot crontab");return NULL;}return raw;
}
static cJSON *external_cron_entries(const char *raw,bool *valid,bool *truncated){
 cJSON *entries=cJSON_CreateArray();char *copy=strdup(raw),*save=NULL;bool inside=false,seen=false;unsigned count=0;*valid=true;*truncated=false;
 if(!copy){cJSON_Delete(entries);*valid=false;return NULL;}
 for(char *line=strtok_r(copy,"\n",&save);line;line=strtok_r(NULL,"\n",&save)){
  if(!strcmp(line,BEGIN)){if(inside||seen){*valid=false;break;}inside=true;seen=true;continue;}
  if(!strcmp(line,END)){if(!inside){*valid=false;break;}inside=false;continue;}
  if(inside)continue;
  const char *p=line;while(*p==' '||*p=='\t')p++;if(!*p||*p=='#')continue;
  if(count++>=128){*truncated=true;continue;}
  const char *command=p;int fields=*p=='@'?1:5;bool parsed=true;
  for(int i=0;i<fields;i++){while(*command==' '||*command=='\t')command++;if(!*command){parsed=false;break;}while(*command&&*command!=' '&&*command!='\t')command++;}
  while(*command==' '||*command=='\t')command++;
  if(!*command)parsed=false;
  const char *kind=parsed?"custom_command":"unknown",*label=parsed?"Custom command":"Unparsed cron entry";
  if(parsed&&!strncmp(command,"/root/bin/zigma-time-sync ",sizeof("/root/bin/zigma-time-sync ")-1)){kind="time_sync";label="Time synchronization";}
  else if(parsed&&!strcmp(command,"/root/bin/zigmactl reboot")){kind="reboot";label="Reboot robot";}
  else if(parsed&&!strcmp(command,"/root/bin/zigmactl clean")){kind="clean";label="Full clean";}
  else if(parsed&&!strcmp(command,"/root/bin/zigmactl dock")){kind="dock";label="Return to dock";}
  else if(parsed&&!strncmp(command,SCHEDULE_RANDOM,strlen(SCHEDULE_RANDOM))&&(command[strlen(SCHEDULE_RANDOM)]==0||isspace((unsigned char)command[strlen(SCHEDULE_RANDOM)]))){kind="random_song";label="Random song";}
  else if(parsed&&!strncmp(command,SCHEDULE_PLAYER " " SCHEDULE_SONGS "/",strlen(SCHEDULE_PLAYER " " SCHEDULE_SONGS "/"))){kind="play_song";label="Play song";}
  else if(parsed&&(!strncmp(command,"/root/bin/zigmactl clean ",strlen("/root/bin/zigmactl clean ")))){const char *v=command+strlen("/root/bin/zigmactl clean ");if(*v>='1'&&*v<='3'&&(!v[1]||isspace((unsigned char)v[1]))){kind="clean";label="Full clean";}}
  cJSON *entry=cJSON_CreateObject();setstr(entry,"kind",kind);setstr(entry,"label",label);setstr(entry,"raw",line);setbool(entry,"managed",false);setbool(entry,"editable",false);
  if(parsed)setstr(entry,"command",command);
  cJSON_AddItemToArray(entries,entry);
 }
 if(inside)*valid=false;
 free(copy);if(!*valid){cJSON_Delete(entries);return NULL;}return entries;
}
