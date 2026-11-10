#include <dirent.h>
/* Direct cron actions chosen explicitly by the authenticated user. */
#define SCHEDULE_SONGS "/root/zigma-free/songs"
#define SCHEDULE_RANDOM SCHEDULE_SONGS "/play_random_song.sh"
#define SCHEDULE_PLAYER "/root/bin/audio_player"
static bool schedule_extra(cJSON *job){const char *a=str(job,"action","");return !strcmp(a,"random_song")||!strcmp(a,"play_song")||!strcmp(a,"custom_command");}
static bool schedule_song_name(const char *s){
 size_t n=strlen(s);if(n<5||n>100||s[0]=='.')return false;
 for(const unsigned char *p=(const unsigned char*)s;*p;p++)if(!isalnum(*p)&&*p!='_'&&*p!='-'&&*p!='.')return false;
 return !strcasecmp(s+n-4,".mp3")||!strcasecmp(s+n-4,".wav");
}
static bool schedule_command_valid(const char *s){
 if(!*s||strlen(s)>256)return false;
 bool nonblank=false;for(const unsigned char *p=(const unsigned char*)s;*p;p++){if(*p<32||*p==127)return false;if(*p!=' ')nonblank=true;}return nonblank;
}
/* Quoting is for sh -c; the user's shell syntax is interpreted only at run time. */
static void schedule_quote(char *out,const char *s){*out++='\'';for(;*s;s++){if(*s=='\''){memcpy(out,"'\\''",4);out+=4;}else *out++=*s;}*out++='\'';*out=0;}
static cJSON *schedule_song_list(void){
 cJSON *a=cJSON_CreateArray();DIR *d=opendir(SCHEDULE_SONGS);if(!d)return a;
 struct dirent *e;unsigned count=0;while((e=readdir(d))&&count<256){if(!schedule_song_name(e->d_name))continue;char p[512];struct stat st;snprintf(p,sizeof p,"%s/%s",SCHEDULE_SONGS,e->d_name);if(lstat(p,&st)||!S_ISREG(st.st_mode))continue;cJSON_AddItemToArray(a,cJSON_CreateString(e->d_name));count++;}closedir(d);return a;
}
