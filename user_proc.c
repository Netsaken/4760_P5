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
    unsigned int creationTimeSecs, creationTimeNS, initialSharedSecs, initialSharedNS, initialTermSecs, initialTermNS, newTimeNS, termTimeNS;
    int initSwitch = 1, ownedSwitch = 0, termSwitch = 1;
    int randomRsrcPos, randomRsrcStorage;
    int resourcesObtained[10] = {0};
    int requestChance = 50, luckyRelease = 30, terminationChance = 10;

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

    //Set time interval bounds
    int B = 15000000; //15 milliseconds
    int D = 250000000; //250 milliseconds

    //Set creation time, so process stays alive for at least 1 second
    creationTimeSecs = *sharedSecs;
    creationTimeNS = *sharedSecs;

    for (;;) {
        //If a resource was requested, spin until it's given
        for (int s = 0; s < 10; s++) {
            if (resourceTbl->reqMtx[i][s] > 0) {
                while (resourceTbl->reqMtx[i][s] > 0) {}
            }
        }

        //Get a random time interval (only if it is not already set)
        if (initSwitch == 1) {
            initialSharedSecs = *sharedSecs;
            initialSharedNS = *sharedNS;

            newTimeNS = rand() % (B + 1);
            initSwitch = 0;
        }

        //Get separate time interval for self-termination
        if (termSwitch == 1) {
            initialTermSecs = *sharedSecs;
            initialTermNS = *sharedNS;

            termTimeNS = rand() % (D + 1);
            termSwitch = 0;
        }

        /********************************************************************************************************************
        If the clock has hit the random time, decide to request or release a resource
        *********************************************************************************************************************/
        if (((*sharedSecs * BILLION) + *sharedNS) > ((initialSharedSecs * BILLION) + initialSharedNS + newTimeNS)) {
            //Do a dice roll on which resource to request
            randomRsrcPos = rand() % 10;

            //Do a dice roll on whether to request or release a resource
            if ((rand() % 100) < requestChance) {
                //Request a random amount of a random resource
                randomRsrcStorage = (rand() % resourceTbl->rsrcVec[randomRsrcPos]) + 1;
                resourceTbl->reqMtx[i][randomRsrcPos] += randomRsrcStorage;
                
                //Can't continue until resource is granted, so assume it's being granted
                resourcesObtained[randomRsrcPos] += randomRsrcStorage;
                ownedSwitch = 1;
            } else if (ownedSwitch == 1) {
                //Release a random amount of a random resource you control (keep running a dice roll on each possible value until one releases)
                for (int l = 0;;l++) {
                    if (resourcesObtained[l] != 0) {
                        if ((rand() % 100) < luckyRelease) {
                            randomRsrcStorage = (rand() % resourcesObtained[l]) + 1;

                            resourceTbl->reqMtx[i][l] -= randomRsrcStorage;
                            resourcesObtained[l] -= randomRsrcStorage;
                            break;
                        }
                    }
                    
                    if (l >= 9) {
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

        /********************************************************************************************************************
        If the clock has hit the other random time, decide whether to DIE DRAMATICALLY
        *********************************************************************************************************************/
        if ((((*sharedSecs * BILLION) + *sharedNS) > ((creationTimeSecs * BILLION) + creationTimeNS + BILLION)) &&
           (((*sharedSecs * BILLION) + *sharedNS) > ((initialTermSecs * BILLION) + initialTermNS + termTimeNS))) {
            //Do a russian roulette dice roll
            if ((rand() % 100) < terminationChance) {
                resourceTbl->reqMtx[i][0] = -30;
                endProcess();
            }

            termSwitch = 1;
        }
    }

    endProcess();

    /**********************************************************************************
   
    End doing things here

    ***********************************************************************************/

    return 0;
}