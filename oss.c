#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define BILLION 1000000000UL //1 second in nanoseconds
#define VERBOSE 1; //1 is on, 0 is off

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
    // printf("REQUEST MATRIX:\n");
    // printf("\tR0\tR1\tR2\tR3\tR4\tR5\tR6\tR7\tR8\tR9\n");
    //             for (int t = 0; t < 18; t++) {
    //                 printf("P%i", t);
    //                 for (int u = 0; u < 10; u++) {
    //                     printf("\t %i", resourceTbl->reqMtx[t][u]);
    //                 }
    //                 printf("\n");
    //             }
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

bool req_lt_avail(const int *avail, const int pnum, const int num_res)
{
    int i = 0;
    for (; i < num_res; i++) {
        if (resourceTbl->reqMtx[pnum][i] > avail[i]) {
            break;
        }
    }

    // if (i != num_res) {
    //     printf("On return, i is %i, num_res is %i\n", i, num_res);
    //     printf("\tand the value is %s\n", (i == num_res) ? "true" : "false");
    // }
    return (i == num_res);
}

bool deadlock(const int m, const int n, const int allocated[18][10], const int alloVec[10])
{
    int work[m];    // m resources
    bool finish[n]; // n processes
    for (int i = 0; i < m; i++) {
        work[i] = alloVec[i]; //Available resources
    }
    for (int i = 0; i < n; finish[i++] = false);
    
    int p = 0;
    for (; p < n; p++)
    {
        if (finish[p] == true) { continue; }
        if (req_lt_avail(work, p, m) == true)
        {
            finish[p] = true;
            for (int i = 0; i < m; i++) {
                work[i] += allocated[p][i];
            }
            p = -1;
        }
    }

    for (p = 0; p < n; p++) {
        if (finish[p] == false) {
            break;
        }
    }

    // for (int x = 0; x < 10; x++) {
    //     printf("%i ", work[x]);
    // }
    // puts("");
    // bool check = false;
    // for (int x = 0; x < 18; x++) {
    //     if (finish[x] != true) {
    //         check = true;
    //         break;
    //     }    
    // }
    // if (check == true) {
    //     for (int x = 0; x < 18; x++) {
    //         printf("%s ", finish[x] ? "true" : "false");
    //     }
    //     puts("");
    // }

    return (p != n);
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
    int maxProcsHit = 0, requestCount = 0, lineCount = 0;

    unsigned long initialTimeNS, initialTimeSecs, deadlockInitSecs, deadlockInitNS;
    unsigned int randomTimeNS = 0;
    int initSwitch = 1, deadlockSwitch = 1;

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

    //Add processes to blocked queue when they're stuck waiting
    int blockedQueue[18] = {0};
    
    //Randomize values for shared resource vector (total resources)
    srand(time(NULL));
    for (int i = 0; i < 10; i++) { 
        resourceTbl->rsrcVec[i] = (rand() % 20) + 1; //Between 1 and 20, inclusive
    }

    //Initialize Allocation vector and matrix
    int alloVec[10];
    memcpy(alloVec, resourceTbl->rsrcVec, sizeof(alloVec));
    int alloMtx[18][10] = {0};

    //Open file (closed in handle_sig())
    file = fopen("LOGFile.txt", "w");

    //Loop until alarm rings
    for (;;) {
        //Get a random time interval for process creation (only if it is not already set)
        if (initSwitch == 1) {
            initialTimeSecs = *sharedSecs;
            initialTimeNS = *sharedNS;

            randomTimeNS = (rand() % 499000000) + 1000000; //Between 1 and 500 milliseconds, inclusive
            initSwitch = 0;
        }

        //Get random time interval for deadlock detection
        if (deadlockSwitch == 1) {
            deadlockInitSecs = *sharedSecs;
            deadlockInitNS = *sharedNS;

            deadlockSwitch = 0;
        }

        //Add ambient time to the clock
        *sharedNS += 100000; //0.1 milliseconds
        if (*sharedNS >= BILLION) {
            *sharedSecs += 1;
            *sharedNS -= BILLION;
        }

        /********************************************************************************************************************
        Check to see if a resource has been requested or released
        *********************************************************************************************************************/
        for (int p = 0; p < 18; p++) {
            //Skip Blocked processes
            if (blockedQueue[p] < 0) {
                continue;
            }
            
            for (int r = 0; r < 10; r++) {
                //Add time to the clock
                *sharedNS += 1000; //0.001 milliseconds
                if (*sharedNS >= BILLION) {
                    *sharedSecs += 1;
                    *sharedNS -= BILLION;
                }

                //If the number is greater than 0, it's a request
                if (resourceTbl->reqMtx[p][r] > 0) {
                    //Log to file
                    if (lineCount < 100000) {
                        fprintf(file, "Master detecting Process P%i REQUESTING resources at clock time %li:%09li\n", p, (long)*sharedSecs, (long)*sharedNS);
                        ++lineCount;
                    }

                    //If resource is available, grant it
                    if (alloVec[r] > 0) {
                        ++requestCount;

                        //Increase value in allocation matrix and decrease vector
                        alloMtx[p][r] += 1;
                        alloVec[r] -= 1;

                        //Log to file
                        if (lineCount < 100000) {
                            fprintf(file, "\tMaster granting Resource R%i to Process P%i at clock time %li:%09li\n", r, p, (long)*sharedSecs, (long)*sharedNS);
                            ++lineCount;
                        }

                        //Decrease value in request matrix
                        //printf("\tThe value at [%i][%i] was %i at clock time %li:%09li\n", p, r, resourceTbl->reqMtx[p][r], (long)*sharedSecs, (long)*sharedNS);
                        resourceTbl->reqMtx[p][r] = 0;
                    } else {
                        //Log to file
                        if (lineCount < 100000) {
                            fprintf(file, "\tResource R%i is unavailable, Process P%i is Blocked at clock time %li:%09li\n", r, p, (long)*sharedSecs, (long)*sharedNS);
                            ++lineCount;
                        }

                        //If resource is unavailable, put process in Blocked queue and stop checking for them until resources free up
                        blockedQueue[p] = -1;
                    }
                //If the number is -30, that process has terminated on its own
                } else if (resourceTbl->reqMtx[p][r] == -30) {
                    //Log to file
                    if (lineCount < 100000) {
                        fprintf(file, "Master detecting Process P%i HAS TERMINATED at clock time %li:%09li\n", p, (long)*sharedSecs, (long)*sharedNS);
                        ++lineCount;
                    }

                    //Reset values in request matrix
                    for (int t = 0; t < 10; t++) {
                        resourceTbl->reqMtx[p][t] = 0;
                    }

                    //Remove pid from table
                    resourceTbl->pidArray[p] = 0;

                    //Log resources released and reset values
                    if (lineCount < 100000) {
                        fprintf(file, "Resources released: ");
                        for (int u = 0; u < 10; u++) {
                            if (alloMtx[p][u] > 0) {
                                fprintf(file, "R%i:%i \t", u, alloMtx[p][u]);

                                alloVec[u] += alloMtx[p][u];
                                alloMtx[p][u] = 0;
                            }
                        }
                        fprintf(file, "\n");
                        ++lineCount;
                    }

                //If the number is less than 0, it's a release
                } else if (resourceTbl->reqMtx[p][r] < 0) {
                    //Log to file
                    if (lineCount < 100000) {
                        fprintf(file, "Master detecting Process P%i RELEASING resources at clock time %li:%09li\n", p, (long)*sharedSecs, (long)*sharedNS);
                        ++lineCount;
                    }

                    //Decrease value in allocation matrix and increase vector
                    alloMtx[p][r] -= 1;
                    alloVec[r] += 1;

                    //Log to file
                    if (lineCount < 100000) {
                        fprintf(file, "\tResources released: R%i:1\n", r);
                        ++lineCount;
                    }

                    //Reset Blocked queue to check if their resources can be granted
                    for (int b = 0; b < 18; b++) {
                        blockedQueue[b] = 0;
                    }

                    //Increase value in request matrix
                    resourceTbl->reqMtx[p][r] += 1;
                }
            }
        }

        /********************************************************************************************************************
        If the clock has hit the random time, make a new process
        *********************************************************************************************************************/
        //Before creating child, reset iInc value to the first available empty slot in table (and check if max processes has been hit)
        for (int j = 0; j < 18; j++) {
            if (resourceTbl->pidArray[j] == 0) {
                iInc = j;
                maxProcsHit = 0;
                break;
            }
            maxProcsHit = 1;
        }

        if (maxProcsHit == 0 && (((*sharedSecs * BILLION) + *sharedNS) > ((initialTimeSecs * BILLION) + initialTimeNS + randomTimeNS))) {
            //Add time to the clock
            *sharedNS += 500000; //0.5 milliseconds
            if (*sharedNS >= BILLION) {
                *sharedSecs += 1;
                *sharedNS -= BILLION;
            }

            //Before creating child, reset iInc value to the first available empty slot in table
            for (int j = 0; j < 18; j++) {
                if (resourceTbl->pidArray[j] == 0) {
                    iInc = j;
                    maxProcsHit = 0;
                    break;
                }
                maxProcsHit = 1;
            }

            // printf("Checking value of alloVec: ");
            // for (int i = 0; i < 10; i++) {
            //     printf("%i ", alloVec[i]);
            // }
            // printf("\n");

            //Create a user process
            childPid = fork();
            if (childPid == -1) {
                strcpy(report, ": childPid");
                message = strcat(title, report);
                perror(message);
                return 1;
            }

            // Allocate and execute
            if (childPid == 0) {
                sprintf(iNum, "%i", iInc);
                execl("./user_proc", iNum, NULL);
            } else {
                // Store childPid
                resourceTbl->pidArray[iInc] = childPid;

                // Log to file
                if (lineCount < 100000) {
                    fprintf(file, "Master creating new Process P%i at clock time %li:%09li\n", iInc, (long)*sharedSecs, (long)*sharedNS);
                    ++lineCount;
                }
            }

            //Reset switch
            initSwitch = 1;
        }

        /********************************************************************************************************************
        Run Deadlock Detection Algorithm every simulated second
        *********************************************************************************************************************/
        if (((*sharedSecs * BILLION) + *sharedNS) > ((deadlockInitSecs * BILLION) + deadlockInitNS + (BILLION))) { //Initial time plus 1 second
            //Add time to the clock
            *sharedNS += 1000000; //1 millisecond
            if (*sharedNS >= BILLION) {
                *sharedSecs += 1;
                *sharedNS -= BILLION;
            }

            //Log to file
            if (lineCount < 100000) {
                fprintf(file, "Master running deadlock detection at time %li:%09li\n", (long)*sharedSecs, (long)*sharedNS);
                ++lineCount;
            }

            //Detect deadlocks
            for (;;) {
                if (deadlock(10, 18, alloMtx, alloVec) == true) {
                    //Log to file
                    if (lineCount < 100000) {
                        fprintf(file, "DEADLOCK DETECTED: Processes ");
                        for (int i = 0; i < 18; i++) {
                            if (blockedQueue[i] < 0) {
                                fprintf(file, "P%i ", i);
                            }
                        }
                        fprintf(file, "deadlocked\n");
                        ++lineCount;
                    }

                    //Terminate the first process in Blocked queue and return resources
                    for (int t = 0; t < 18; t++) {
                        if (blockedQueue[t] < 0) {
                            //Log to file
                            if (lineCount < 100000) {
                                fprintf(file, "\tMaster terminating Process P%i\n", t);
                                ++lineCount;
                            }

                            kill(resourceTbl->pidArray[t], SIGTERM);
                            resourceTbl->pidArray[t] = 0;

                            if (lineCount < 100000) {
                                fprintf(file, "Resources released: ");
                                for (int r = 0; r < 10; r++) {
                                    if (alloMtx[t][r] > 0) {
                                        fprintf(file, "R%i:%i \t", r, alloMtx[t][r]);

                                        alloVec[r] += alloMtx[t][r];
                                        alloMtx[t][r] = 0;
                                    }
                                }
                                fprintf(file, "\n");
                                ++lineCount;
                            }

                            //Remove from Blocked queue
                            blockedQueue[t] = 0;

                            break;
                        }
                    }

                    continue;
                } else {
                    //Log to file
                    if (lineCount < 100000) {
                        fprintf(file, "\tNo deadlock detected\n");
                        ++lineCount;
                    }
                    break;
                }
            }

            //Reset switch
            deadlockSwitch = 1;
        }

        /********************************************************************************************************************
        Print table every 20 granted requests
        *********************************************************************************************************************/
        if (lineCount < 100000 && requestCount > 20) {
            fprintf(file, "RESOURCE VECTOR: ");
            for (int a = 0; a < 10; a++) {
                fprintf(file, "\t%i ", resourceTbl->rsrcVec[a]);
            }
            fprintf(file, "\n");
        }

        if (lineCount < 100000 && requestCount > 20) {
            fprintf(file, "ALLOCATION VECTOR: ");
            for (int a = 0; a < 10; a++) {
                fprintf(file, "\t%i ", alloVec[a]);
            }
            fprintf(file, "\n");
        }

        if (lineCount < 100000) {
            if (requestCount > 20) {
                fprintf(file, "\tR0\tR1\tR2\tR3\tR4\tR5\tR6\tR7\tR8\tR9\n");
                ++lineCount;
                for (int t = 0; t < 18; t++) {
                    fprintf(file, "P%i", t);
                    for (int u = 0; u < 10; u++) {
                        fprintf(file, "\t %i", alloMtx[t][u]);
                    }
                    fprintf(file, "\n");
                    ++lineCount;
                }

                requestCount = 0;
            }
        }
    }

    /**********************************************************************************
   
    End doing things here

    ***********************************************************************************/

    return 0;
}