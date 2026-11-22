/* Timed/leased manual control. Native queue exit; no SPI RX-to-TX replay. */
#include <dirent.h>
#include <limits.h>
#include "zigma_app_profile.h"
#include "zigma_native_identity.h"
#include "zigma_process.h"

static int manual_app, manual_nav;
static unsigned long long manual_app_tick, manual_nav_tick;
static bool manual_nav_hash(int pid) {
    char path[64]; snprintf(path,sizeof path,"/proc/%d/exe",pid);
    int fd=open(path,O_RDONLY|O_CLOEXEC); if(fd<0)return false;
    mbedtls_sha256_context ctx; mbedtls_sha256_init(&ctx);
    int rc=mbedtls_sha256_starts(&ctx,0); unsigned char buf[16384],digest[32]; ssize_t n=0;
    while(!rc&&(n=read(fd,buf,sizeof buf))>0)rc=mbedtls_sha256_update(&ctx,buf,(size_t)n);
    if(n<0)rc=1;
    if(!rc)rc=mbedtls_sha256_finish(&ctx,digest);
    close(fd);mbedtls_sha256_free(&ctx); if(rc)return false;
    char hash[65]; for(int i=0;i<32;i++)snprintf(hash+2*i,3,"%02x",digest[i]);
    return !strcmp(hash,"b10fc7b48d1bdb990a2c37b82f7d45fa4fd7358f428f42d58ae885aca2970255");
}
static bool manual_native_profile(void) {
    char mac[32]={0};FILE *mf=fopen("/sys/class/net/wlan0/address","r");
    if(!mf)return false;
    bool target=fgets(mac,sizeof mac,mf)&&!strncmp(mac,"34:20:03:44:61:d1",17);fclose(mf);
    if(!target||geteuid()!=0)return false;
    DIR *d=opendir("/proc"); if(!d)return false;
    int app=0,nav=0,na=0,nnative=0;struct dirent *e;
    while((e=readdir(d))){int pid;if(!nav_int(e->d_name,1,INT_MAX,&pid))continue;
        if(zigma_native_process(pid,"/root/app_server/wifi_app_server_grit")){app=pid;na++;}
        if(zigma_native_process(pid,"/root/hoslam/demo_rplidar")){nav=pid;nnative++;}
    }
    closedir(d); if(na!=1||nnative!=1)return false;
    unsigned long long at=zigma_start_ticks(app),nt=zigma_start_ticks(nav);
    const struct zigma_app_profile *profile=zigma_app_profile_process(app);
    if(!at||!nt||!profile||strcmp(profile->name,"zigma")||!manual_nav_hash(nav))return false;
    if(at!=zigma_start_ticks(app)||nt!=zigma_start_ticks(nav))return false;
    manual_app=app;manual_nav=nav;manual_app_tick=at;manual_nav_tick=nt;return true;
}
static bool manual_native_alive(void) {
    return manual_app_tick&&manual_nav_tick&&zigma_start_ticks(manual_app)==manual_app_tick&&
        zigma_start_ticks(manual_nav)==manual_nav_tick&&
        zigma_native_process(manual_app,"/root/app_server/wifi_app_server_grit")&&
        zigma_native_process(manual_nav,"/root/hoslam/demo_rplidar");
}
#include "zigma_manual_exit.h"
static bool (*manual_read_sample)(struct manual_sample *)=manual_sample_native;
static int (*manual_event)(bool,double)=manual_event_native;
static bool (*manual_profile_check)(void)=manual_native_profile;
static bool (*manual_alive_check)(void)=manual_native_alive;
static int manual_send(const char *work) {
    char json[160];snprintf(json,sizeof json,"{\"ProtocolVersion\":\"1.0\",\"WorkStatus\":\"%s\",\"cmd_origin\":\"app\"}",work);
    return nav_json(1,json);
}
/* Both messages use the same ordered JSON queue: stop the app timer, then
 * request native motion pause. Success means queued, not physically stopped. */
static int manual_stop_messages(void) {
    int a=manual_send("MoveStop"),b=manual_send("Pause");return a||b?1:0;
}
static int manual_gate(void) {
    int fd=open(nav_path("ZIGMA_MANUAL_GATE","/tmp/zigma-manual.gate"),O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0)return -1;
    struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)){close(fd);return -1;}return fd;
}
static int manual_generation(int fd,uint64_t *v,bool bump) {
    *v=0;ssize_t n=pread(fd,v,sizeof *v,0);
    if(n!=0&&n!=(ssize_t)sizeof *v)return -1;
    if(bump){if(*v==UINT64_MAX)return -1;(*v)++;if(pwrite(fd,v,sizeof *v,0)!=(ssize_t)sizeof *v)return -1;}
    return 0;
}
static bool manual_ready(bool initial) {
    cJSON *s=nav_status();const char *w=ns(s,"working_status"),*error=ns(s,"error");
    const char *reason=NULL;
    if(!manual_alive_check())reason="native process identity changed or unavailable";
    else if(!nav_link(s))reason="observer/cache communication missing or older than 15 seconds";
    else if(strcmp(error,"NoError"))reason="robot reports an error or error status is missing";
    else if(strcmp(w,"Standby")&&strcmp(w,"Pause")&&strcmp(w,"CleanDone")&&
            (initial||strcmp(w,"DirectionControl")))reason="working status does not permit manual control";
    if(reason)fprintf(stderr,"Manual control rejected: %s (working_status=%s error=%s cache_age=%.0fs link_age=%.0fs)\n",
        reason,*w?w:"missing",*error?error:"missing",(double)time(NULL)-nn(s,"cache_read_at"),(double)time(NULL)-nn(s,"local_link_received_at"));
    cJSON_Delete(s);return reason==NULL;
}
static const char *manual_initial_reason(const struct manual_sample *s) {
    if(s->app_error)return "native app reports an error";
    /* 70 is CleanDone in the verified Zigma app profile. Keep all native guards. */
    if(s->app_state!=2&&s->app_state!=70)return "native app must be Standby (2) or CleanDone (70)";
    if(s->state!=0&&s->state!=26)return "navigation must be idle (0) or paused (26)";
    if(s->timer)return "native direction timer is still active";
    if(!s->neutral)return "wheel commands are not neutral";
    if(!s->empty)return "native event queue is not empty";
    return NULL;
}
static bool manual_initial_check(void) {
    struct manual_sample s;
    if(!manual_read_sample(&s)){fprintf(stderr,"Manual control rejected: cannot read a consistent native state\n");return false;}
    const char *reason=manual_initial_reason(&s);
    if(reason)fprintf(stderr,"Manual control rejected: %s (app_state=%u app_error=%u nav_state=%u timer=%u neutral=%d queue_empty=%d)\n",
        reason,s.app_state,s.app_error,s.state,s.timer,s.neutral,s.empty);
    return reason==NULL;
}
static void manual_wait(void){struct timespec ts={0,20000000};nanosleep(&ts,NULL);}
/* Never report a queued event as a confirmed controller exit. Hashes were
 * validated once at entry; PID/start/executable identity is rechecked throughout.
 * Budget is for software cleanup, not a physical braking-time guarantee. */
static int manual_finish(void) {
    double begun=nav_clock(),deadline=begun+2.5,quiet=-1;
    int transport=manual_stop_messages();struct manual_sample s;
    bool saw_manual=false;
    while(nav_clock()<deadline){
        if(!manual_alive_check()||!manual_read_sample(&s))goto unconfirmed;
        if(s.app_state==21)saw_manual=true;
        if(!s.timer){if(quiet<0)quiet=nav_clock();if(nav_clock()-quiet>=0.25)break;}
        else quiet=-1;
        if(nav_clock()-begun>=0.6)goto unconfirmed;
        manual_wait();
    }
    if(s.timer||nav_clock()>=deadline)goto unconfirmed;
    if(s.state!=7){
        if((s.state==0||s.state==26)&&s.neutral&&s.empty&&s.app_state==2&&!s.app_error)
            return transport?1:0;
        goto unconfirmed;
    }
    if(!s.active||!s.empty)goto unconfirmed;
    if(manual_event(true,deadline))goto unconfirmed;
    for(;;){
        if(nav_clock()>=deadline||!manual_alive_check()||!manual_read_sample(&s))goto unconfirmed;
        if(s.app_state==21)saw_manual=true;
        if(s.timer||s.state!=7)goto unconfirmed;
        if(s.neutral&&s.empty&&s.last==0xa1)break;
        manual_wait();
    }
    if(manual_event(false,deadline))goto unconfirmed;
    for(;;){
        if(nav_clock()>=deadline||!manual_alive_check()||!manual_read_sample(&s))goto unconfirmed;
        if(s.timer)goto unconfirmed;
        if(s.state==26&&s.neutral&&s.empty&&s.app_state==2&&!s.app_error&&saw_manual){
            fprintf(stderr,"Manual exit confirmed by controller in %.0f ms; physical braking not measured\n",(nav_clock()-begun)*1000);
            return transport?1:0;
        }
        manual_wait();
    }
unconfirmed:
    fprintf(stderr,"Manual exit UNCONFIRMED after %.0f ms; inspect robot, no automatic retry or reboot\n",(nav_clock()-begun)*1000);
    return 8;
}
static bool manual_gate_lock(int fd) {
    double until=nav_clock()+3.0;
    do {
        if(!flock(fd,LOCK_EX|LOCK_NB))return true;
        if(errno!=EWOULDBLOCK&&errno!=EAGAIN&&errno!=EINTR)return false;
        manual_wait();
    }while(nav_clock()<until);
    return false;
}
/* Local pipe contract for the future web adapter: '.' renews a 750ms lease,
 * '!' releases, EOF releases, invalid input fails closed. Browser keepalives
 * must be forwarded only for an authenticated, currently pressed button. */
static int manual_lease_native(double *expires) {
    struct pollfd p={.fd=STDIN_FILENO,.events=POLLIN};int r=poll(&p,1,0);
    if(r<0)return errno==EINTR?0:-1;
    if(!r)return 0;
    if(p.revents&(POLLERR|POLLNVAL))return -1;
    if(p.revents&(POLLIN|POLLHUP)){
        char b[64];ssize_t n=read(STDIN_FILENO,b,sizeof b);
        if(n==0)return 2;
        if(n<0)return errno==EINTR||errno==EAGAIN?0:-1;
        if(n>16)return -1;
        bool renewal=false;
        for(ssize_t i=0;i<n;i++){
            if(b[i]=='!')return 2;
            if(b[i]=='.')renewal=true;
            else if(b[i]!='\n'&&b[i]!='\r')return -1;
        }
        if(renewal){*expires=nav_clock()+0.75;return 1;}
    }
    return 0;
}
static int (*manual_lease)(double *)=manual_lease_native;
static int manual_stop(void) {
    if(!manual_profile_check()){fprintf(stderr,"Unsupported or unavailable manual-control firmware\n");return 7;}
    int fd=manual_gate();if(fd<0)return 1;
    uint64_t generation;int rc=1;
    if(manual_gate_lock(fd)){
        /* Serialize cancellation with every direction renewal, so no old
         * driver can enqueue another direction after this stop. */
        int invalidated=manual_generation(fd,&generation,true);
        rc=manual_finish();if(invalidated&&rc==0)rc=1;
        flock(fd,LOCK_UN);
    }
    close(fd);if(!rc)puts("Controller reports manual mode exited; physical stop not measured");return rc;
}
static const char *manual_direction(const char *s) {
    if(!strcmp(s,"forward"))return "MoveFront";
    if(!strcmp(s,"backward"))return "MoveBack";
    if(!strcmp(s,"left"))return "MoveLeft";
    if(!strcmp(s,"right"))return "MoveRight";
    return NULL;
}
static int manual_run(const char *work,int milliseconds,bool held) {
    if(!manual_profile_check()){fprintf(stderr,"Unsupported or unavailable manual-control firmware\n");return 7;}
    int lock=open(nav_path("ZIGMA_NAV_LOCK","/tmp/zigma-navigation.lock"),O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
    if(lock<0||flock(lock,LOCK_EX|LOCK_NB)){if(lock>=0)close(lock);fprintf(stderr,"Another navigation command is running\n");return 6;}
    int fd=manual_gate(),rc=1;uint64_t token=0;
    if(fd<0){close(lock);return 1;}
    if(!manual_gate_lock(fd))goto done;
    if(!manual_ready(true)){rc=3;flock(fd,LOCK_UN);goto done;}
    if(!manual_initial_check()){rc=3;flock(fd,LOCK_UN);goto done;}
    if(manual_generation(fd,&token,true)){flock(fd,LOCK_UN);goto done;}
    /* Install handlers before any direction is queued. */
    nav_cancelled=0;struct sigaction sa={0},oldint,oldterm,oldhup;
    sa.sa_handler=nav_signal;sigemptyset(&sa.sa_mask);
    if(sigaction(SIGINT,&sa,&oldint)){flock(fd,LOCK_UN);goto done;}
    if(sigaction(SIGTERM,&sa,&oldterm)){sigaction(SIGINT,&oldint,NULL);flock(fd,LOCK_UN);goto done;}
    if(sigaction(SIGHUP,&sa,&oldhup)){sigaction(SIGINT,&oldint,NULL);sigaction(SIGTERM,&oldterm,NULL);flock(fd,LOCK_UN);goto done;}
    double until=nav_clock()+(held?0.75:milliseconds/1000.0),next=0;bool renewed=!held,sent=false;
    flock(fd,LOCK_UN);rc=0;
    while(nav_clock()<until&&!nav_cancelled){
        if(held){int lease=manual_lease(&until);if(lease==2)break;if(lease<0){rc=2;break;}if(lease==1)renewed=true;}

        if(!manual_gate_lock(fd)){rc=1;break;}
        uint64_t current;int bad=manual_generation(fd,&current,false);
        if(bad||current!=token){rc=bad?1:130;flock(fd,LOCK_UN);break;}
        if(!manual_ready(false)){rc=3;flock(fd,LOCK_UN);break;}
        if(renewed&&!nav_cancelled&&nav_clock()<until&&nav_clock()>=next){
            sent=true; /* A failing send still requires cancellation of a partial session. */
            if(manual_send(work)){rc=1;flock(fd,LOCK_UN);break;}
            next=nav_clock()+0.5;
        }
        flock(fd,LOCK_UN);manual_wait();
    }
    if(nav_cancelled)rc=130;
    if(manual_gate_lock(fd)){
        uint64_t current;
        int bad=manual_generation(fd,&current,false);
        if(sent&&(bad||current==token)){int finish=manual_finish();if(finish)rc=finish;}
        /* A separate stop already cancelled this generation; do not affect
         * any newer session by sending a second cleanup stop. */
        flock(fd,LOCK_UN);
    }else rc=1;
    sigaction(SIGINT,&oldint,NULL);sigaction(SIGTERM,&oldterm,NULL);sigaction(SIGHUP,&oldhup,NULL);
    done:close(fd);close(lock);
    if(!rc)puts("Manual session ended; controller exit verified when motion was requested");
    else fprintf(stderr,"Manual control ended (exit %d); inspect robot state\n",rc);
    return rc;
}
static int manual_drive(const char *work,int milliseconds){return manual_run(work,milliseconds,false);}
static int manual_check(void){
    if(!manual_profile_check()){fprintf(stderr,"Unsupported or unavailable manual-control firmware\n");return 7;}
    puts("Manual preflight (read only; no commands or events sent)");
    bool ready=manual_ready(true),native=manual_initial_check();
    if(!ready||!native)return 3;
    printf("Manual preflight passed: supported profile, fresh idle app, neutral wheels, timer off and empty queue (app=%d nav=%d); state may change before drive\n",manual_app,manual_nav);
    return 0;
}
static bool manual_duration(const char *text,int *ms){
    size_t n=strlen(text);
    if(n>1&&text[n-1]=='s'){
        char b[24];if(n>=sizeof b)return false;memcpy(b,text,n-1);b[n-1]=0;
        char *end;errno=0;double seconds=strtod(b,&end);
        if(errno||!*b||*end||!isfinite(seconds)||seconds<0.1||seconds>10)return false;
        *ms=(int)lround(seconds*1000);return true;
    }
    return nav_int(text,100,10000,ms);
}
static int manual_cli(int argc,char **argv) {
    if(!strcmp(argv[1],"stop"))return argc==2?manual_stop():2;
    if(!strcmp(argv[1],"manual-check"))return argc==2?manual_check():2;
    if(!strcmp(argv[1],"hold")){
        const char *w=argc==3?manual_direction(argv[2]):NULL;
        if(!w||isatty(STDIN_FILENO)){fprintf(stderr,"Usage: zigmactl hold direction < renewal-pipe ('.' every <=250ms, '!' or EOF to stop)\n");return 2;}
        return manual_run(w,0,true);
    }
    const char *work=argc==4?manual_direction(argv[2]):NULL;int ms;
    if(!work||!manual_duration(argv[3],&ms)){
        fprintf(stderr,"Usage: zigmactl drive {forward|backward|left|right} duration (100..10000 milliseconds or 0.1s..10s), stop, hold direction, manual-check\n");return 2;
    }
    return manual_drive(work,ms);
}
