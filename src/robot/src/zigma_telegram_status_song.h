/* On-demand diagnostics and one asynchronous, owned MP3 player. */
static const char *tg_songs_dir="/root/zigma-free/songs";
static const char *tg_player="/usr/bin/madplay";
static pid_t tg_song_pid;
static long tg_song_started;
static void tg_song_tick(void){
 if(!tg_song_pid)return;
 int status=0;pid_t done=waitpid(tg_song_pid,&status,WNOHANG);
 if(done==tg_song_pid){tg_song_pid=0;if(!WIFEXITED(status)||WEXITSTATUS(status))tg_reply("Song playback failed.");}
 else if(done<0){tg_song_pid=0;}
 else if(tg_monotonic()-tg_song_started>=900){kill(tg_song_pid,SIGKILL);waitpid(tg_song_pid,NULL,0);tg_song_pid=0;tg_reply("Song stopped after the 15-minute limit.");}
}
static void tg_song_stop(void){if(tg_song_pid){kill(tg_song_pid,SIGKILL);waitpid(tg_song_pid,NULL,0);tg_song_pid=0;}}
static void tg_play_song(const char *arg){
 if(*arg&&(strlen(arg)!=1||*arg<'1'||*arg>'5')){tg_reply("Usage: /playsong [1-5]");return;}
 tg_song_tick();if(tg_song_pid){tg_reply("A song is already playing. Wait until it finishes.");return;}
 int number=*arg?*arg-'0':0;
 if(!number){unsigned char b;int fd=open("/dev/urandom",O_RDONLY|O_CLOEXEC);if(fd<0){tg_reply("Random selection unavailable.");return;}ssize_t n;do{n=read(fd,&b,1);}while(n==1&&b==255);close(fd);if(n!=1){tg_reply("Random selection unavailable.");return;}number=1+b%5;}
 char file[1024],reply[160];snprintf(file,sizeof file,"%s/%d.mp3",tg_songs_dir,number);
 struct stat st;if(lstat(file,&st)||!S_ISREG(st.st_mode)||!st.st_size||access(file,R_OK)){snprintf(reply,sizeof reply,"Song %d.mp3 is missing or unreadable in the songs folder.",number);tg_reply(reply);return;}
 if(!strcmp(mode,"demo")){snprintf(reply,sizeof reply,"Song %d.mp3 simulated (no audio).",number);tg_reply(reply);return;}
 if(access(tg_player,X_OK)){tg_reply("MP3 player unavailable.");return;}
 pid_t parent=getpid(),child=fork();
 if(child==0){
  prctl(PR_SET_PDEATHSIG,SIGKILL);if(getppid()!=parent)_exit(127);
  int fd=open("/dev/null",O_RDWR);if(fd<0)_exit(127);for(int i=0;i<3;i++)dup2(fd,i);if(fd>2)close(fd);
  execl(tg_player,tg_player,"-q",file,(char*)NULL);_exit(127);
 }
 if(child<0){tg_reply("Could not start MP3 player.");return;}
 tg_song_pid=child;tg_song_started=tg_monotonic();snprintf(reply,sizeof reply,"Playback requested: %d.mp3",number);tg_reply(reply);
}
static void tg_get_status(void){
 cJSON *s=status_json();char battery[32]="unavailable",uptime[80]="unavailable",load[80]="unavailable",ram[120]="unavailable",disk[120]="unavailable";
 cJSON *b=get(s,"battery_level");if(cJSON_IsNumber(b)&&isfinite(b->valuedouble)&&b->valuedouble>=0&&b->valuedouble<=100)snprintf(battery,sizeof battery,"%.0f%%",b->valuedouble);
 FILE *f=fopen("/proc/uptime","r");double seconds;if(f){if(fscanf(f,"%lf",&seconds)==1&&seconds>=0){unsigned long t=(unsigned long)seconds;snprintf(uptime,sizeof uptime,"%lud %02lu:%02lu:%02lu",t/86400,t/3600%24,t/60%60,t%60);}fclose(f);}
 f=fopen("/proc/loadavg","r");double a,c,d;if(f){if(fscanf(f,"%lf %lf %lf",&a,&c,&d)==3)snprintf(load,sizeof load,"%.2f / %.2f / %.2f",a,c,d);fclose(f);}
 f=fopen("/proc/meminfo","r");unsigned long long total=0,available=0,free_k=0,buffers=0,cached=0,reclaim=0,shared=0;bool has_available=false;
 if(f){char line[256],key[64];unsigned long long value;while(fgets(line,sizeof line,f)){if(sscanf(line,"%63s %llu",key,&value)!=2)continue;
  if(!strcmp(key,"MemTotal:"))total=value;else if(!strcmp(key,"MemAvailable:")){available=value;has_available=true;}else if(!strcmp(key,"MemFree:"))free_k=value;else if(!strcmp(key,"Buffers:"))buffers=value;else if(!strcmp(key,"Cached:"))cached=value;else if(!strcmp(key,"SReclaimable:"))reclaim=value;else if(!strcmp(key,"Shmem:"))shared=value;
 }fclose(f);if(!has_available){available=free_k+buffers+cached+reclaim;available=available>shared?available-shared:0;}if(available>total)available=total;
 if(total)snprintf(ram,sizeof ram,"%.1f / %.1f MiB (%.1f%%)",(total-available)/1024.0,total/1024.0,100.0*(total-available)/total);}
 struct statvfs v;if(!statvfs(data_dir,&v)&&v.f_blocks){double total_b=(double)v.f_blocks*v.f_frsize,used=(double)(v.f_blocks-v.f_bfree)*v.f_frsize;snprintf(disk,sizeof disk,"%.1f / %.1f MiB (%.1f%%)",used/1048576,total_b/1048576,100*used/total_b);}
 char wifi[40]="unavailable";cJSON *w=get(s,"wifi");
 if(cJSON_IsTrue(get(w,"available")))snprintf(wifi,sizeof wifi,"%d dBm",num(w,"signal_dbm",0));
 char message[1000];snprintf(message,sizeof message,"Status: %.64s\nError: %.64s\nBattery: %s\nWi-Fi: %s\nUptime: %s\nCPU load: %s\nRAM: %s\nDisk: %s",str(s,"working_status","unknown"),str(s,"error","unknown"),battery,wifi,uptime,load,ram,disk);
 if(tg_enqueue(message,0)){
  cJSON *job=cJSON_GetArrayItem(tg_queue,cJSON_GetArraySize(tg_queue)-1);
  char id[33],file[512];bool attached=false;
  /* Explicit status requests include the map regardless of notification filters. */
  if(tg_temp()&&tg_random(id)){
   char name[48];snprintf(name,sizeof name,"%s.png",id);tg_file(file,sizeof file,name);
   if(tg_map_render(file,0,get(s,"navigation_pose"),time(NULL))){setstr(job,"photo",file);attached=true;}
  }
  if(!attached){char caption[1100];snprintf(caption,sizeof caption,"%s\nMap/position unavailable.",message);setstr(job,"text",caption);}
 }
 cJSON_Delete(s);
}
