#define main network_main
#include "../src/zigma_network.c"
#undef main
#include <assert.h>
static void fixture(const char *dir,const char *name,char state){
 char p[1024];snprintf(p,sizeof p,"%s/123",dir);mkdir(p,0700);snprintf(p,sizeof p,"%s/123/comm",dir);FILE *f=fopen(p,"w");assert(f);fprintf(f,"%s\n",name);fclose(f);
 snprintf(p,sizeof p,"%s/123/stat",dir);f=fopen(p,"w");assert(f);fprintf(f,"123 (%s) %c 42",name,state);for(int i=5;i<22;i++)fputs(" 0",f);fputs(" 999\n",f);fclose(f);
}
int main(void){
 char dir[]="/tmp/network-owner.XXXXXX";assert(mkdtemp(dir));procroot=dir;
 const char *names[]={"ygclient","daemon","wpa_supplicant","udhcpc","hostapd","udhcpd"};
 for(unsigned i=0;i<6;i++){
  fixture(dir,names[i],'Z');assert(foreign_owner()==0&&ignored_zombies==1);
  fixture(dir,names[i],'X');assert(foreign_owner()==0);
  fixture(dir,names[i],'S');assert(foreign_owner()==1&&conflict.pid==123&&conflict.ppid==42&&conflict.start==999&&conflict.state=='S'&&!strcmp(conflict.name,names[i]));
 }
 wpa_pid=123;assert(foreign_owner()==0);wpa_pid=0;
 fixture(dir,"unrelated",'S');assert(foreign_owner()==0);
 fixture(dir,"wpa_supplicant",'?');assert(foreign_owner()==-1&&owner_scan_errno);
 char p[1024];snprintf(p,sizeof p,"%s/123/stat",dir);unlink(p);assert(foreign_owner()==0); /* vanished after comm */
 fixture(dir,"wpa_supplicant",'S');FILE *f=fopen(p,"w");fputs("123 (renamed) S 42\n",f);fclose(f);assert(foreign_owner()==-1);
 fixture(dir,"wpa_supplicant",'S');assert(foreign_owner()==1);snprintf(state_path,sizeof state_path,"%s/status.json",dir);assert(publish("ownership_conflict","",0));
 f=fopen(state_path,"r");char json[4096];assert(fgets(json,sizeof json,f));fclose(f);assert(strstr(json,"\"conflict_ppid\":42")&&strstr(json,"\"conflict_name\":\"wpa_supplicant\""));unlink(state_path);
 unlink(p);snprintf(p,sizeof p,"%s/123/comm",dir);unlink(p);snprintf(p,sizeof p,"%s/123",dir);rmdir(p);
 /* A real unreaped zombie must not block; only expose this child to the scan. */
 pid_t child=fork();assert(child>=0);if(!child){prctl(PR_SET_NAME,"wpa_supplicant",0,0,0);_exit(0);}siginfo_t info;assert(!waitid(P_PID,child,&info,WEXITED|WNOWAIT));
 char target[64];snprintf(target,sizeof target,"/proc/%d",child);snprintf(p,sizeof p,"%s/%d",dir,child);assert(!symlink(target,p));assert(foreign_owner()==0&&ignored_zombies==1);waitpid(child,NULL,0);assert(foreign_owner()==0);unlink(p);rmdir(dir);
 puts("PASS: six zombie/live owner classes, dead state, owned child, unrelated process, malformed/renamed/vanished stat, diagnostic fields, real zombie and reaping");return 0;
}
