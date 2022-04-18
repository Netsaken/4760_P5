#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/resource.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define BILLION 1000000000UL //1 second in nanoseconds

pid_t childPid;

unsigned int *sharedNS;
unsigned int *sharedSecs;
struct PCT *procCtl = {0};
int shmid_NS, shmid_Secs, shmid_PCT;
int msqid;
int queueSize = 0;

static void handle_sig(int sig) {
    int errsave, status;
    errsave = errno;
    //Print notification
    printf("Program interrupted. Shutting down...\n");

    //End children
    if (childPid != 0) {
        kill(childPid, SIGTERM);
        waitpid(childPid, &status, 0);
    }

    //Detach shared memory
    if (shmdt(sharedNS) == -1) {
        perror("./oss: sigShmdtNS");
    }

    if (shmdt(sharedSecs) == -1) {
        perror("./oss: sigShmdtSecs");
    }

    //Remove shared memory
    if (shmctl(shmid_NS, IPC_RMID, 0) == -1) {
        perror("./oss: sigShmctlNS");
    }

    if (shmctl(shmid_Secs, IPC_RMID, 0) == -1) {
        perror("./oss: sigShmctlSecs");
    }

    // Remove message queue
    if (msgctl(msqid, IPC_RMID, NULL) == -1)
    {
        perror("./oss: sigMsgctl");
    }

    printf("Cleanup complete. Have a nice day!\n");

    //Exit program
    errno = errsave;
    exit(0);
}

static int setupinterrupt(void) {
    struct sigaction act;
    act.sa_handler = handle_sig;
    act.sa_flags = 0;
    return (sigemptyset(&act.sa_mask) || sigaction(SIGALRM, &act, NULL));
}

struct my_msgbuf {
   long mtype;
   char mtext[10];
};

int main(int argc, char *argv[])
{
    signal(SIGINT, handle_sig);
    signal(SIGABRT, handle_sig);

    FILE *file;
    struct my_msgbuf buf;

    key_t keyNS = ftok("./README.txt", 'Q');
    key_t keySecs = ftok("./README.txt", 'b');
    key_t keyMsg = ftok("./user_proc.c", 't');
    char iNum[3];
    int iInc = 0;

    struct timespec currentTime;
    unsigned long initialTimeNS, elapsedTimeNS, initialQuantumNS;
    unsigned int randomTimeNS = 0, randomTimeSecs = 0;
    int initSwitch = 1;
    
    const unsigned int maxTimeBetweenNewProcsNS = 500000000;
    const unsigned int maxTimeBetweenNewProcsSecs = 0;

    //Format "perror"
    char* title = argv[0];
    char report[30] = ": shm";
    char* message;

    //Set up interrupts
    if (setupinterrupt() == -1) {
        strcpy(report, ": setupinterrupt");
        message = strcat(title, report);
        perror(message);
        abort();
    }

    //Get shared memory
    shmid_NS = shmget(keyNS, sizeof(sharedNS), IPC_CREAT | 0666);
    if (shmid_NS == -1) {
        strcpy(report, ": shmgetNS");
        message = strcat(title, report);
        perror(message);
        abort();
    }

    shmid_Secs = shmget(keySecs, sizeof(sharedSecs), IPC_CREAT | 0666);
    if (shmid_Secs == -1) {
        strcpy(report, ": shmgetSecs");
        message = strcat(title, report);
        perror(message);
        abort();
    }

    //Attach shared memory
    sharedNS = shmat(shmid_NS, NULL, 0);
    if (sharedNS == (void *) -1) {
        strcpy(report, ": shmatNS");
        message = strcat(title, report);
        perror(message);
        abort();
    }

    sharedSecs = shmat(shmid_Secs, NULL, 0);
    if (sharedSecs == (void *) -1) {
        strcpy(report, ": shmatSecs");
        message = strcat(title, report);
        perror(message);
        abort();
    }

    //Get message queue
    if ((msqid = msgget(keyMsg, 0666 | IPC_CREAT)) == -1) {
        strcpy(report, ": msgget");
        message = strcat(title, report);
        perror(message);
        abort();
    }

    /********************************************************************************

    Start doing things here

    *********************************************************************************/
    file = fopen("LOGFile.txt", "w");

    /* START THE CL0CK */
    //Get the random time interval (only if it is not already set)
    if (initSwitch == 1) {
        randomTimeSecs = rand() % (maxTimeBetweenNewProcsSecs + 1);
        randomTimeNS = rand() % maxTimeBetweenNewProcsNS;

        clock_gettime(CLOCK_MONOTONIC, &currentTime);
        initialTimeNS = (currentTime.tv_sec * BILLION) + currentTime.tv_nsec;
        initSwitch = 0;
    }

    //Count the time
    clock_gettime(CLOCK_MONOTONIC, &currentTime);
    elapsedTimeNS = ((currentTime.tv_sec * BILLION) + currentTime.tv_nsec) - initialTimeNS;

    // Create a user process
    childPid = fork();
    if (childPid == -1) {
        strcpy(report, ": childPid");
        message = strcat(title, report);
        perror(message);
        abort();
    }

    // Allocate block, add to queue, and execute process
    if (childPid == 0) {
        sprintf(iNum, "%i", iInc);
        execl("./user_proc", iNum, NULL);
    }
    else
    {
        sleep(1);
    }

    fclose(file);

    /**********************************************************************************
   
    End doing things here

    ***********************************************************************************/

    //Remove message queue
    if (msgctl(msqid, IPC_RMID, NULL) == -1)
    {
        strcpy(report, ": msgctl");
        message = strcat(title, report);
        perror(message);
        abort();
    }

    //Detach shared memory
    if (shmdt(sharedNS) == -1) {
        strcpy(report, ": shmdtNS");
        message = strcat(title, report);
        perror(message);
        abort();
    }

    if (shmdt(sharedSecs) == -1) {
        strcpy(report, ": shmdtSecs");
        message = strcat(title, report);
        perror(message);
        abort();
    }

    //Remove shared memory
    if (shmctl(shmid_NS, IPC_RMID, 0) == -1) {
        strcpy(report, ": shmctlNS");
        message = strcat(title, report);
        perror(message);
        abort();
    }

    if (shmctl(shmid_Secs, IPC_RMID, 0) == -1) {
        strcpy(report, ": shmctlSecs");
        message = strcat(title, report);
        perror(message);
        abort();
    }

    return 0;
}