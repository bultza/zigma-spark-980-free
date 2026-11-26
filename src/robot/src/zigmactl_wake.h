/* Experimental clean/pause wake. No GPIO, firmware patch or persistent mute. */
#ifndef ZIGMACTL_WAKE_H
#define ZIGMACTL_WAKE_H
#include <sys/mount.h>
#include <sys/stat.h>
#include <poll.h>
#include <limits.h>

#ifndef ZIGMA_WAKE_LOCK
#define ZIGMA_WAKE_LOCK "/tmp/zigma-navigation.lock"
#endif
#ifndef WAKE_MADPLAY
#define WAKE_MADPLAY "/usr/bin/madplay"
#endif
#ifndef ZIGMA_WAKE_STATUS_PATH
#define ZIGMA_WAKE_STATUS_PATH "/tmp/zigma-free/observation/status.json"
#endif
enum wake_state { WAKE_UNKNOWN, WAKE_ASLEEP, WAKE_IDLE, WAKE_ACTIVE };
struct wake_snapshot { enum wake_state state; double generation; };
struct wake_ops {
    void *ctx;
    double (*clock)(void *);
    struct wake_snapshot (*snapshot)(void *);
    int (*mute)(void *);
    int (*unmute)(void *);
    int (*send)(void *, unsigned char, unsigned char);
    int (*alive)(void *);
    void (*wait)(void *, int);
};
/* Return 0 only for already-awake or independently observed stable idle.
 * 3 unavailable state; 4 unconfirmed exit; 5 mute/transport failure;
 * 6 busy; 7 audio restoration failure; 130 cancellation. */
/* Dispatch deadlines are independent of telemetry and confirmation. These are
 * command deadlines, not proof of wheel stop or hard real-time guarantees. */
static void wake_wait_until(struct wake_ops *o, double deadline)
{
    while(o->clock(o->ctx)<deadline) {
        double left=(deadline-o->clock(o->ctx))*1000.0;
        int ms=left>50?50:(int)ceil(left);
        if(ms>0)o->wait(o->ctx,ms);
    }
}
static int wake_run(struct wake_ops *o)
{
    struct wake_snapshot s=o->snapshot(o->ctx);
    if(s.state==WAKE_IDLE || s.state==WAKE_ACTIVE) {
        puts("Robot already awake; no command sent"); return 0;
    }
    if(s.state!=WAKE_ASLEEP) {
        fprintf(stderr,"Wake refused: recent, supported robot state required\n"); return 3;
    }
    if(!o->alive(o->ctx)) return 130;
    if(o->mute(o->ctx)) {
        fprintf(stderr,"Cannot silence madplay; no cleaning command sent\n"); return 5;
    }
    int rc=5;
    double last_pause=-1;
    s=o->snapshot(o->ctx);
    if(s.state!=WAKE_ASLEEP) { rc=3; goto out; }
    if(!o->alive(o->ctx)) { rc=130; goto out; }
    if(o->send(o->ctx,0x24,2)) goto out;
    double start=o->clock(o->ctx);
    rc=o->send(o->ctx,0x22,2)?5:0; /* Attempt clean once only. */
    int fan_repeated=0;
    /* Do not mistake an intermediate Standby for completion of clean startup.
     * Use the requested fixed two-second first-pause deadline. */
    while(!rc && o->clock(o->ctx)<start+2.0) {
        if(!o->alive(o->ctx)) { rc=130; break; }
        if(!fan_repeated && o->clock(o->ctx)>=start+0.5) {
            if(o->send(o->ctx,0x24,2)) {rc=5;break;}
            fan_repeated=1;
        }
        double next=o->clock(o->ctx)+0.05;
        wake_wait_until(o,next<start+2.0?next:start+2.0);
    }
    /* First pause at 2s, or immediately on error/caller loss. Always send the
     * second at 3.5s from clean, even if snapshots are missing or the caller died.
     * All cleanup command attempts finish BEFORE observation-only verification. */
    for(int attempt=0;attempt<2;attempt++) {
        if(attempt)wake_wait_until(o,start+3.5);
        int failed=o->send(o->ctx,0x26,0);
        last_pause=o->clock(o->ctx);
        fprintf(stderr,"Wake pause %d/2 %s at +%.3fs; effect not yet confirmed\n",
                attempt+1,failed?"dispatch FAILED":"dispatched",last_pause-start);
        if(failed)rc=5;
    }
    if(!o->alive(o->ctx) && !rc)rc=130;
    if(rc)goto out;
    double first_idle=-1,last_generation=-1;
    int idle_samples=0;
    rc=4;
    /* From here on NO commands are sent, on timeout or cancellation either. */
    while(o->clock(o->ctx)-last_pause<10.0) {
        if(!o->alive(o->ctx)) {rc=130;break;}
        s=o->snapshot(o->ctx);
        if(s.state==WAKE_UNKNOWN) {rc=3;break;}
        if(s.state==WAKE_IDLE && s.generation>last_pause) {
            if(first_idle<0)first_idle=o->clock(o->ctx);
            if(s.generation>last_generation){idle_samples++;last_generation=s.generation;}
            if(idle_samples>=2 && o->clock(o->ctx)-first_idle>=1.0 &&
               o->clock(o->ctx)-last_pause>=2.0){rc=0;break;}
        } else {first_idle=-1;idle_samples=0;last_generation=-1;}
        o->wait(o->ctx,100);
    }
out:
    if(last_pause>=0)wake_wait_until(o,last_pause+2.0);
    if(o->unmute(o->ctx)) {
        fprintf(stderr,"Audio restoration FAILED; inspect mount on " WAKE_MADPLAY "\n");
        return 7;
    }
    if(!rc)puts("Wake confirmed: stable awake idle state; audio restored");
    else fprintf(stderr,"Wake not confirmed (exit %d); no late pause, clean retry or reboot\n",rc);
    return rc;
}

static struct wake_snapshot wake_decode(cJSON *s, double monotonic)
{
    struct wake_snapshot r={WAKE_UNKNOWN,nn(s,"observer_monotonic_ms")/1000.0};
    if(!nav_link(s) || !isfinite(r.generation) || monotonic-r.generation<0 ||
       monotonic-r.generation>5 || strcmp(ns(s,"working_status_source"),"robot_work_state_cache")) return r;
    /* Status-report error fields can belong to a different, older state.
     * Only treat an error as current when its state matches the work cache. */
    if(nn(s,"vendor_state_code")==nn(s,"working_state_code") && strcmp(ns(s,"error"),"NoError")) return r;
    const char *w=ns(s,"working_status");
    if(!strcmp(w,"Hibernating")) r.state=WAKE_ASLEEP;
    else if(!strcmp(w,"Standby")||!strcmp(w,"Pause")||!strcmp(w,"CleanDone")||
            !strcmp(w,"PileCharging")||!strcmp(w,"DirCharging")) r.state=WAKE_IDLE;
    else if(!strcmp(w,"AutoClean")||!strcmp(w,"SpotClean")||!strcmp(w,"PlanningRect")||
            !strcmp(w,"SelectClean")||!strcmp(w,"DirectionControl")||!strcmp(w,"BackCharging")||
            !strcmp(w,"PlanningLocation")||!strcmp(w,"Relocation")) r.state=WAKE_ACTIVE;
    return r;
}
struct wake_context { int parent; char directory[80],stub[100]; struct stat mounted; int bound; };
static volatile sig_atomic_t wake_cancelled;
static void wake_signal(int sig) { (void)sig; wake_cancelled=1; }
static double wake_clock(void *ctx) { (void)ctx; return nav_clock(); }
static struct wake_snapshot wake_snapshot(void *ctx)
{
    (void)ctx;
    /* No environment override: wake must never trust a test fixture on hardware. */
    cJSON *s=nav_read(ZIGMA_WAKE_STATUS_PATH);
    struct wake_snapshot r=wake_decode(s,nav_clock()); cJSON_Delete(s); return r;
}
static int wake_mounted(void)
{
    FILE *f=fopen("/proc/self/mountinfo","r"); if(!f)return -1;
    char line[4096],target[PATH_MAX]; int found=0;
    while(fgets(line,sizeof line,f)) {
        if(sscanf(line,"%*s %*s %*s %*s %4095s",target)==1 && !strcmp(target,WAKE_MADPLAY)) found=1;
    }
    if(ferror(f))found=-1;
    fclose(f);return found;
}
static int wake_mute(void *opaque)
{
    struct wake_context *c=opaque; struct stat st;
    if(lstat(WAKE_MADPLAY,&st)||!S_ISREG(st.st_mode)||wake_mounted()!=0) return -1;
    strcpy(c->directory,"/tmp/zigma-wake-XXXXXX");
    if(!mkdtemp(c->directory))return -1;
    snprintf(c->stub,sizeof c->stub,"%s/madplay",c->directory);
    int fd=open(c->stub,O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC|O_NOFOLLOW,0700);
    const char content[]="#!/bin/sh\nexit 0\n";
    int ok=fd>=0 && write(fd,content,sizeof content-1)==(ssize_t)(sizeof content-1) &&
        !fchmod(fd,0700) && !fstat(fd,&c->mounted);
    if(fd>=0)close(fd);
    if(!ok)goto failed;
    /* A noexec /tmp or unavailable interpreter must fail before any clean. */
    pid_t test=fork();
    if(test==0){execl(c->stub,c->stub,(char *)NULL);_exit(127);}
    int status=0; pid_t waited=-1;
    if(test>0)do {waited=waitpid(test,&status,0);} while(waited<0&&errno==EINTR);
    if(test<0||waited!=test||!WIFEXITED(status)||WEXITSTATUS(status))goto failed;
    if(mount(c->stub,WAKE_MADPLAY,NULL,MS_BIND,NULL))goto failed;
    c->bound=1;
    return 0;
failed:
    unlink(c->stub);rmdir(c->directory);return -1;
}
static int wake_unmute(void *opaque)
{
    struct wake_context *c=opaque;struct stat st;
    if(c->bound) {
        /* Do not detach a different mount installed by somebody else. */
        if(stat(WAKE_MADPLAY,&st)||st.st_dev!=c->mounted.st_dev||st.st_ino!=c->mounted.st_ino)return -1;
        if(umount2(WAKE_MADPLAY,MNT_DETACH))return -1;
        c->bound=0;
    }
    unlink(c->stub);rmdir(c->directory);return 0;
}
static int wake_send(void *ctx,unsigned char op,unsigned char arg)
{ (void)ctx;return send_command(2,op,arg); }
static int wake_alive(void *opaque)
{
    struct wake_context *c=opaque;struct pollfd p={c->parent,POLLIN|POLLHUP,0};
    if(wake_cancelled)return 0;
    int n=poll(&p,1,0);
    return n==0 || (n<0&&errno==EINTR);
}
static void wake_wait(void *ctx,int ms)
{ (void)ctx;struct timespec t={ms/1000,(ms%1000)*1000000L};while(nanosleep(&t,&t)&&errno==EINTR){} }
static int wake_lock(void)
{
    int fd=open(ZIGMA_WAKE_LOCK,O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
    struct stat st;
    if(fd<0)return -1;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||flock(fd,LOCK_EX|LOCK_NB)) {close(fd);return -1;}
    return fd;
}
static int ctl_wake(bool check)
{
    int lock=wake_lock();if(lock<0){fprintf(stderr,"Another control operation is running\n");return 6;}
    if(check) {
        struct wake_snapshot s=wake_snapshot(NULL);
        printf("Wake preflight (read only): state=%s; madplay_mount=%d; no commands sent\n",
            s.state==WAKE_ASLEEP?"hibernating":s.state==WAKE_IDLE?"awake-idle":s.state==WAKE_ACTIVE?"awake-active":"unavailable",wake_mounted());
        close(lock);return s.state==WAKE_UNKNOWN?3:0;
    }
    if(geteuid()!=0){close(lock);fprintf(stderr,"Wake requires root\n");return 5;}
    int pipefd[2];if(pipe(pipefd)){close(lock);return 5;}
    fflush(NULL);
    pid_t guardian=fork();
    if(guardian<0){close(lock);close(pipefd[0]);close(pipefd[1]);return 5;}
    if(!guardian) {
        close(pipefd[1]);setsid();
        /* The child owns mount, command sequence and lock. Parent death closes
         * the pipe, so recovery also runs after caller SIGKILL or SSH loss. */
        signal(SIGHUP,wake_signal);signal(SIGINT,wake_signal);signal(SIGTERM,wake_signal);
        signal(SIGPIPE,SIG_IGN);wake_cancelled=0;
        int nullfd=open("/dev/null",O_RDONLY);if(nullfd>=0){dup2(nullfd,0);if(nullfd>2)close(nullfd);}
        struct wake_context c={.parent=pipefd[0]};
        struct wake_ops o={&c,wake_clock,wake_snapshot,wake_mute,wake_unmute,wake_send,wake_alive,wake_wait};
        int rc=wake_run(&o);fflush(NULL);close(pipefd[0]);close(lock);_exit(rc);
    }
    close(pipefd[0]);close(lock);
    struct sigaction a={0},old_hup,old_int,old_term;
    a.sa_handler=wake_signal;sigemptyset(&a.sa_mask);wake_cancelled=0;
    sigaction(SIGHUP,&a,&old_hup);sigaction(SIGINT,&a,&old_int);sigaction(SIGTERM,&a,&old_term);
    int status=0;pid_t ended;
    do {
        if(wake_cancelled&&pipefd[1]>=0){close(pipefd[1]);pipefd[1]=-1;}
        ended=waitpid(guardian,&status,0);
    } while(ended<0&&errno==EINTR);
    if(pipefd[1]>=0)close(pipefd[1]);
    sigaction(SIGHUP,&old_hup,NULL);sigaction(SIGINT,&old_int,NULL);sigaction(SIGTERM,&old_term,NULL);
    return ended==guardian&&WIFEXITED(status)?WEXITSTATUS(status):7;
}
#endif
