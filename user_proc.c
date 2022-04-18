#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/resource.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>

#define QUANTUM 10000000
#define BILLION 1000000000UL //1 second in nanoseconds
#define CHANCE_TO_TERMINATE 20
#define BLOCK_CHANCE 5

unsigned int *sharedNS;
unsigned int *sharedSecs;
struct PCT *procCtl = {0};

struct my_msgbuf {
   long mtype;
   char mtext[10];
};

struct PCB {
    unsigned int totalCPUTimeUsed;
    unsigned int totalTimeInSystem;
    unsigned int lastTimeUsed;
    //Pid to be set with "getpid();"
    pid_t thisPid;
    int priority;
    int iValue;
};

struct PCT {
    struct PCB ctrlTbl[18];
    int blocksInUse[18];
};

void endProcess() {
    //Detach shared memory
    if (shmdt(sharedNS) == -1) {
        perror("./user_proc: endShmdtNS");
        exit(1);
    }

    if (shmdt(sharedSecs) == -1) {
        perror("./user_proc: endShmdtSecs");
        exit(1);
    }

    exit(0);
}

int main(int argc, char *argv[])
{
    struct my_msgbuf buf;
    int shmid_NS, shmid_Secs;
    unsigned int initialSharedSecs, initialSharedNS, interTImeNS;

    int msqid;
    int i = atoi(argv[0]);

    key_t keyNS = ftok("./README.txt", 'Q');
    key_t keySecs = ftok("./README.txt", 'b');
    key_t keyMsg = ftok("./user_proc.c", 't');

    //Format "perror"
    char* title = argv[0];
    char report[30] = ": shm";
    char* message;

    //Get shared memory
    shmid_NS = shmget(keyNS, sizeof(sharedNS), IPC_CREAT | 0666);
    if (shmid_NS == -1) {
        strcpy(report, ": shmgetNS");
        message = strcat(title, report);
        perror(message);
        return 1;
    }

    shmid_Secs = shmget(keySecs, sizeof(sharedSecs), IPC_CREAT | 0666);
    if (shmid_Secs == -1) {
        strcpy(report, ": shmgetSecs");
        message = strcat(title, report);
        perror(message);
        return 1;
    }

    //Attach shared memory
    sharedNS = (unsigned int*)shmat(shmid_NS, NULL, 0);
    if (sharedNS == (void *) -1) {
        strcpy(report, ": shmatNS");
        message = strcat(title, report);
        perror(message);
        return 1;
    }

    sharedSecs = (unsigned int*)shmat(shmid_Secs, NULL, 0);
    if (sharedSecs == (void *) -1) {
        strcpy(report, ": shmatSecs");
        message = strcat(title, report);
        perror(message);
        return 1;
    }

    //Get message queue
    if ((msqid = msgget(keyMsg, 0666 | IPC_CREAT)) == -1) {
        strcpy(report, ": msgget");
        message = strcat(title, report);
        perror(message);
        return 1;
    }

    /********************************************************************************

    Start doing things here

    *********************************************************************************/

    printf("Hi! o/\nMy name is Proccy! :B\n");

    endProcess();

    /**********************************************************************************
   
    End doing things here

    ***********************************************************************************/

    return 0;
}