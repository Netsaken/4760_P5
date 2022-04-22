#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>

#define BILLION 1000000000UL //1 second in nanoseconds

unsigned int *sharedNS;
unsigned int *sharedSecs;
struct RT *resourceTbl = {0};

struct RT {
    pid_t pidArray[18];
    int reqMtx[18][10];
    int rsrcVec[10];
};

void endProcess() {
    //Detach shared memory
    if (shmdt(sharedNS) == -1) {
        perror("./user_proc: endShmdtNS");
    }

    if (shmdt(sharedSecs) == -1) {
        perror("./user_proc: endShmdtSecs");
    }

    if (shmdt(resourceTbl) == -1) {
        perror("./user_proc: endShmdtRsrc");
    }

    exit(0);
}

int main(int argc, char *argv[])
{
    int shmid_NS, shmid_Secs, shmid_Rsrc;
    unsigned int initialSharedSecs, initialSharedNS, newTimeSecs, newTimeNS;

    int msqid;
    int i = atoi(argv[0]);

    key_t keyNS = ftok("./README.txt", 'Q');
    key_t keySecs = ftok("./README.txt", 'b');
    key_t keyRsrc = ftok(".user_proc.c", 't');

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

    shmid_Rsrc = shmget(keyRsrc, sizeof(resourceTbl), IPC_CREAT | 0666);
    if (shmid_Rsrc == -1) {
        strcpy(report, ": shmgetRsrc");
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

    resourceTbl = shmat(shmid_Rsrc, NULL, 0);
    if (resourceTbl == (void *) -1) {
        strcpy(report, ": shmatRsrc");
        message = strcat(title, report);
        perror(message);
        return 1;
    }

    /********************************************************************************

    Start doing things here

    *********************************************************************************/

    printf("Hi there! o/ My name is Proccy %i! :B\n", i);
    printf("Here the array:\n");
    for (int j = 0; j < 10; j++) {
        printf("%i ", resourceTbl->rsrcVec[j]);
    }
    printf("\n");

    // while(1) {
    //     printf("Testing Sigterm... this should stop... %i\n", i);
    //     sleep(1);
    // }

    endProcess();

    /**********************************************************************************
   
    End doing things here

    ***********************************************************************************/

    return 0;
}