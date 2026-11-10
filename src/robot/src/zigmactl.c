#define _POSIX_C_SOURCE 200809L
#include "zigma_version.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/wait.h>

#define COMMAND_QUEUE_PROJECT 14

struct zigma_msg {
    long mtype;
    unsigned char data[4096];
};

/* Lookup only: never create a queue or send a frame during diagnostics. */
static int command_queue(key_t *resolved)
{
    key_t key = ftok("/", COMMAND_QUEUE_PROJECT);
    if (key == (key_t)-1) { perror("ftok"); return -1; }
    if (resolved) *resolved = key;
    int qid = msgget(key, 0);
    if (qid < 0) perror("msgget");
    return qid;
}

static int check_control(void)
{
    key_t key;
    int qid = command_queue(&key);
    if (qid < 0) return 1;
    printf("Control queue ready: key=0x%08x queue=%d; no command sent\n", (unsigned)key, qid);
    return 0;
}

static int send_command(unsigned char b0,
                        unsigned char b1,
                        unsigned char b2)
{
    int qid = command_queue(NULL);
    if (qid < 0) return 1;
    struct zigma_msg msg;
    uint32_t frame_len = 6;

    memset(&msg, 0, sizeof(msg));

    msg.mtype = 1;

    /* little-endian uint32 frame length */
    msg.data[0] = frame_len & 0xff;
    msg.data[1] = (frame_len >> 8) & 0xff;
    msg.data[2] = (frame_len >> 16) & 0xff;
    msg.data[3] = (frame_len >> 24) & 0xff;

    msg.data[4] = 0xaa;
    msg.data[5] = 0x03;

    msg.data[6] = b0;
    msg.data[7] = b1;
    msg.data[8] = b2;

    msg.data[9] = (unsigned char)(b0 + b1 + b2);

    printf("queue=%d command: "
           "%02x %02x %02x %02x %02x %02x\n",
           qid,
           msg.data[4],
           msg.data[5],
           msg.data[6],
           msg.data[7],
           msg.data[8],
           msg.data[9]);

    if (msgsnd(qid, &msg, sizeof(msg.data), IPC_NOWAIT) < 0) {
        perror("msgsnd");
        return 1;
    }

    return 0;
}

#include "zigma_navigation.h"

static int reboot_robot(void)
{
    int status;

    /*
     * The AM380 is a separate controller.  A plain Linux reboot leaves it
     * running and the two processors can come back out of sync.  This is the
     * same sequence used by the original firmware during an upgrade: hold the
     * AM380 in reset first, and reboot Linux only if that operation succeeds.
     */
    printf("Holding AM380 in reset and rebooting Linux...\n");
    fflush(NULL);

    status = system("/root/bin/hold380 && reboot");
    if (status < 0) {
        perror("system");
        return 1;
    }

    if (WIFEXITED(status)) {
        int exit_status = WEXITSTATUS(status);

        if (exit_status == 0) {
            printf("Reboot requested successfully\n");
            return 0;
        }

        fprintf(stderr,
                "Reboot command failed with exit status %d\n",
                exit_status);
    } else if (WIFSIGNALED(status)) {
        fprintf(stderr,
                "Reboot command terminated by signal %d\n",
                WTERMSIG(status));
    } else {
        fprintf(stderr, "Reboot command did not complete normally\n");
    }

    return 1;
}

int main(int argc, char **argv)
{
    if (zigma_version_only(argc, argv, "zigmactl")) return 0;
    if (argc==2 && !strcmp(argv[1], "check-control")) return check_control();
    int x,y,timeout=300;
    if(argc==2&&!strcmp(argv[1],"getvolume"))return nav_volume(false,0);
    if(argc==3&&!strcmp(argv[1],"setvolume")){
        if(!nav_int(argv[2],0,100,&x))return 2;
        return nav_volume(true,x);
    }
    if(argc>=2&&(!strcmp(argv[1],"goto")||!strcmp(argv[1],"gotoblock"))){
        bool block=!strcmp(argv[1],"gotoblock");
        if((argc!=4&&!(block&&argc==5))||!nav_int(argv[2],-20000,20000,&x)||!nav_int(argv[3],-20000,20000,&y)||(argc==5&&!nav_int(argv[4],10,3600,&timeout)))return 2;
        return nav_move(x,y,false,block,timeout);
    }
    if(argc>=2&&!strcmp(argv[1],"dockblock")){
        if(argc>3||(argc==3&&!nav_int(argv[2],10,3600,&timeout)))return 2;
        return nav_move(0,0,true,true,timeout);
    }
    if(argc>=2&&!strcmp(argv[1],"setheadingblock")){
        fprintf(stderr,"Absolute heading control is not verified; no movement sent\n");return 7;
    }
    if (argc < 2 || argc > 3 || (argc == 3 && strcmp(argv[1], "clean") && strcmp(argv[1], "resume"))) {
        fprintf(stderr,
                "Usage: %s {check-control|clean [1-3]|resume [1-3]|pause|dock|reboot|goto x y|gotoblock x y [timeout_s]|dockblock [timeout_s]|getvolume|setvolume 0-100}\n",
                argv[0]);
        return 1;
    }

    if (!strcmp(argv[1], "clean") ||
        !strcmp(argv[1], "resume")) {
        /* Native fan table: Quiet=2, Normal=0, Strong=1. */
        static const unsigned char fan[] = {2, 0, 1};
        int level = 3;
        if (argc == 3) {
            if (strlen(argv[2]) != 1 || argv[2][0] < '1' || argv[2][0] > '3') {
                fprintf(stderr, "Fan power must be 1, 2 or 3\n");
                return 1;
            }
            level = argv[2][0] - '0';
        }
        if (send_command(0x02, 0x24, fan[level - 1])) return 1;
        return send_command(0x02, 0x22, 0x02);
    }

    if (!strcmp(argv[1], "pause"))
        return send_command(0x02, 0x26, 0x00);

    if (!strcmp(argv[1], "dock"))
        return send_command(0x02, 0x22, 0x03);

    if (!strcmp(argv[1], "reboot"))
        return reboot_robot();

    fprintf(stderr, "Unknown command: %s\n", argv[1]);
    return 1;
}
