#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <time.h>
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
    unsigned int initialSharedSecs, initialSharedNS, newTimeNS;
    int initSwitch = 1, ownedSwitch = 0;
    int randomRsrcPos;
    int resourcesObtained[10] = {0};
    int requestChance = 60, luckyRelease = 30;

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
    //Initialize RNG
    srand(getpid() * time(NULL));

    //Set bound B
    int B = 25000000; //25 milliseconds

    for (;;) {
        //If a resource was requested, spin until it's given
        for (int s = 0; s < 18; s++) {
            if (resourceTbl->reqMtx[i][s] > 0) {
                while (resourceTbl->reqMtx[i][s] > 0) {}
            }
        }

        //Get a random time interval (only if it is not already set)
        if (initSwitch == 1) {
            initialSharedSecs = *sharedSecs;
            initialSharedNS = *sharedNS;

            newTimeNS = rand() % B;
            initSwitch = 0;
        }

        /********************************************************************************************************************
        If the clock has hit the random time, decide to request or release a resource
        *********************************************************************************************************************/
        if (((*sharedSecs * BILLION) + *sharedNS) > ((initialSharedSecs * BILLION) + initialSharedNS + newTimeNS)) {
            //Do a dice roll on which resource to request
            randomRsrcPos = rand() % 10;

            //Do a dice roll on whether to request or release a resource
            if ((rand() % 100) < requestChance) {
                //Request 1 of a random resource
                resourceTbl->reqMtx[i][randomRsrcPos] += 1;
                
                //Can't continue until resource is granted, so assume it's being granted
                resourcesObtained[randomRsrcPos] += 1;
                ownedSwitch = 1;
            } else if (ownedSwitch == 1) {
                //Release 1 of a random resource you control (keep running a dice roll on each possible value until one releases)
                for (int l = 0;;l++) {
                    if (resourcesObtained[l] != 0) {
                        if ((rand() % 100) < luckyRelease) {
                            resourceTbl->reqMtx[i][l] -= 1;
                            resourcesObtained[l] -= 1;
                            break;
                        }
                    }
                    
                    if (l >= 17) {
                        l = 0;
                    }
                }

                //Check to see if any resources are still controlled
                for (int o = 0; o < 10; o++) {
                    if (resourcesObtained[o] != 0) {
                        ownedSwitch = 1;
                        break;
                    } else {
                        ownedSwitch = 0;
                    }
                }
            }

            //Reset timer switch
            initSwitch = 1;
        }
    }


    //printf("Hi there! o/ My name is Proccy %i! :B\n", i);
    // printf("Here the array:\n");
    // for (int j = 0; j < 10; j++) {
    //     printf("%i ", resourceTbl->rsrcVec[j]);
    // }
    // printf("\n");

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