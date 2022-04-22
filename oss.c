#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define BILLION 1000000000UL //1 second in nanoseconds

pid_t childPid;
FILE *file;

unsigned int *sharedNS = 0;
unsigned int *sharedSecs = 0;
struct RT *resourceTbl = {0};
int shmid_NS, shmid_Secs, shmid_Rsrc;

struct RT {
    pid_t pidArray[18];
    int reqMtx[18][10];
    int rsrcVec[10];
};

//Safely terminates program after error, interrupt, or Alarm timer
static void handle_sig(int sig) {
    int errsave, status;
    errsave = errno;
    //Print notification
    printf("Program finished or interrupted. Cleaning up...\n");

    //Close file
    fclose(file);

    //End children
    sleep(1);
    for (int k = 0; k < 18; k++) {
        if (resourceTbl->pidArray[k] != 0) {
            kill(resourceTbl->pidArray[k], SIGTERM);
            waitpid(resourceTbl->pidArray[k], &status, 0);
        }
    }
    // kill(0, SIGTERM);
    // waitpid(childPid, &status, 0);

    //Detach shared memory
    if (shmdt(sharedNS) == -1) {
        perror("./oss: sigShmdtNS");
    }

    if (shmdt(sharedSecs) == -1) {
        perror("./oss: sigShmdtSecs");
    }

    if (shmdt(resourceTbl) == -1) {
        perror("./oss: sigShmdtRsrc");
    }

    //Remove shared memory
    if (shmctl(shmid_NS, IPC_RMID, 0) == -1) {
        perror("./oss: sigShmctlNS");
    }

    if (shmctl(shmid_Secs, IPC_RMID, 0) == -1) {
        perror("./oss: sigShmctlSecs");
    }

    if (shmctl(shmid_Rsrc, IPC_RMID, 0) == -1) {
        perror("./oss: sigShmctlRsrc");
    }

    printf("Cleanup complete!\n");

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

int main(int argc, char *argv[])
{
    signal(SIGINT, handle_sig);
    signal(SIGALRM, handle_sig);

    key_t keyNS = ftok("./README.txt", 'Q');
    key_t keySecs = ftok("./README.txt", 'b');
    key_t keyRsrc = ftok(".user_proc.c", 't');
    char iNum[3];
    int iInc = 0;
    int maxProcsHit = 0;

    unsigned long initialTimeNS, elapsedTimeNS;
    unsigned int randomTimeNS = 0, randomTimeSecs = 0;

    //Format "perror"
    char* title = argv[0];
    char report[30] = ": shm";
    char* message;

    //Set up interrupts
    if (setupinterrupt() == -1) {
        strcpy(report, ": setupinterrupt");
        message = strcat(title, report);
        perror(message);
        return 1;
    }

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
    sharedNS = shmat(shmid_NS, NULL, 0);
    if (sharedNS == (void *) -1) {
        strcpy(report, ": shmatNS");
        message = strcat(title, report);
        perror(message);
        return 1;
    }

    sharedSecs = shmat(shmid_Secs, NULL, 0);
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
    //End program after 2 seconds
    alarm(2);

    //Initialize the other half of the deadlock detection algorithm
    int alloMtx[18][10];
    int alloVec[10];
    for (int i = 0; i < 10; i++) { //Randomize values for shared resource vector (total resources)
        resourceTbl->rsrcVec[i] = (rand() % 20) + 1; //Between 1 and 20, inclusive
    }

    //Open file (closed in handle_sig())
    file = fopen("LOGFile.txt", "w");

    //Loop until alarm rings
    for (;;) {
        //Before creating child, reset iInc value to the first available empty slot in table
        for (int j = 0; j < 18; j++) {
            if (resourceTbl->pidArray[j] == 0) {
                iInc = j;
                maxProcsHit = 0;
                break;
            }
            maxProcsHit = 1;
        }

        //Create a user process
        if (maxProcsHit == 0) {
            childPid = fork();
            if (childPid == -1) {
                strcpy(report, ": childPid");
                message = strcat(title, report);
                perror(message);
                return 1;
            }

            //Allocate block and execute process
            if (childPid == 0) {
                sprintf(iNum, "%i", iInc);
                execl("./user_proc", iNum, NULL);
            } else {
                //Store childPid
                resourceTbl->pidArray[iInc] = childPid;

                sleep(1);
            }
        }
    }

    /**********************************************************************************
   
    End doing things here

    ***********************************************************************************/

    return 0;
}