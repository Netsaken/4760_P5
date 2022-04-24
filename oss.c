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
#define VERBOSE 1 //1 is on, 0 is off

pid_t childPid;
FILE *file;

unsigned int *sharedNS = 0;
unsigned int *sharedSecs = 0;
struct Stats *statistics = {0};
struct RT *resourceTbl = {0};
int shmid_NS, shmid_Secs, shmid_Rsrc, shmid_Stat;

struct Stats {
    int immediateRequests, delayedRequests, deadlockTerminations, naturalTerminations, deadlockRuns;
    float deadlockConsiderations, deadlockTerminationAverage;
};

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

    //Output statistics
    statistics->deadlockTerminationAverage = (float) statistics->deadlockTerminations / statistics->deadlockConsiderations;
    fprintf(file, "\n# of Requests granted immediately: %i\n", statistics->immediateRequests);
    fprintf(file, "# of Requests granted after waiting: %i\n", statistics->delayedRequests);
    fprintf(file, "# of Processes Terminated by Deadlock Detection: %i\n", statistics->deadlockTerminations);
    fprintf(file, "# of Processes Successfully Terminated On Their Own: %i\n", statistics->naturalTerminations);
    fprintf(file, "Deadlock Detection ran %i times, terminating %i processes.\n", statistics->deadlockRuns, statistics->deadlockTerminations);
    fprintf(file, "Deadlock Detection eliminated %f%% of processes on average, out of %i processes considered.", statistics->deadlockTerminationAverage * 100, (int) statistics->deadlockConsiderations);
    fseek(file, 0, SEEK_CUR);
    fflush(file);
    fseek(file, 0, SEEK_CUR);

    //Close file
    fclose(file);

    //End children
    for (int k = 0; k < 18; k++) {
        if (resourceTbl->pidArray[k] != 0) {
            kill(resourceTbl->pidArray[k], SIGTERM);
            waitpid(resourceTbl->pidArray[k], &status, 0);
        }
    }
    sleep(1);

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

    if (shmdt(statistics) == -1) {
        perror("./oss: sigShmdtStat");
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

    if (shmctl(shmid_Stat, IPC_RMID, 0) == -1) {
        perror("./oss: sigShmctlStat");
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

    return (i == num_res);
}

bool deadlock(const int m, const int n, int allocated[18][10], const int alloVec[10])
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

    return (p != n);
}

int main(int argc, char *argv[])
{
    signal(SIGINT, handle_sig);
    signal(SIGALRM, handle_sig);

    key_t keyNS = ftok("./README.txt", 'Q');
    key_t keySecs = ftok("./README.txt", 'b');
    key_t keyRsrc = ftok("./user_proc.c", 'r');
    key_t keyStat = ftok("./user_proc.c", 't');
    char iNum[3];
    int iInc = 0;
    int maxProcsHit = 0, fortyProcs = 0, requestCount = 0, lineCount = 0;

    unsigned long initialTimeNS, initialTimeSecs, deadlockInitSecs, deadlockInitNS;
    unsigned int randomTimeNS = 0;
    int initSwitch = 1, endCheck = 0, deadlockSwitch = 1, deadlockEscape = 0, deadlockClear = 0, collectCheck = 0;

    bool immediateResponse[18] = {0};
    bool dumpCheck;

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

    shmid_Stat = shmget(keyStat, sizeof(statistics), IPC_CREAT | 0666);
    if (shmid_Stat == -1) {
        strcpy(report, ": shmgetStat");
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

    statistics = shmat(shmid_Stat, NULL, 0);
    if (statistics == (void *) -1) {
        strcpy(report, ": shmatStat");
        message = strcat(title, report);
        perror(message);
        return 1;
    }

    /********************************************************************************

    Start doing things here

    *********************************************************************************/
    //End program after 5 seconds
    alarm(5);

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
                    if (alloVec[r] >= resourceTbl->reqMtx[p][r]) {
                        ++requestCount;

                        //Increase value in allocation matrix and decrease vector
                        alloMtx[p][r] += resourceTbl->reqMtx[p][r];
                        alloVec[r] -= resourceTbl->reqMtx[p][r];

                        //Log to file
                        if (lineCount < 100000) {
                            fprintf(file, "\tMaster granting Resources R%i:%i to Process P%i at clock time %li:%09li\n", r, resourceTbl->reqMtx[p][r], p, (long)*sharedSecs, (long)*sharedNS);
                            ++lineCount;
                        }

                        //Reset value in request matrix
                        resourceTbl->reqMtx[p][r] = 0;

                        //Record whether resource was granted immediately or not
                        if (immediateResponse[p] == true) {
                            statistics->immediateRequests += 1;
                        } else {
                            statistics->delayedRequests += 1;
                            immediateResponse[p] = true;
                        }
                    } else {
                        //Log to file
                        if (lineCount < 100000) {
                            fprintf(file, "\tResource R%i:%i is unavailable, Process P%i is Blocked at clock time %li:%09li\n", r, resourceTbl->reqMtx[p][r], p, (long)*sharedSecs, (long)*sharedNS);
                            ++lineCount;
                        }

                        //If resource is unavailable, put process in Blocked queue and stop checking for them until resources free up
                        blockedQueue[p] = -1;

                        //Set flag that resource was delayed
                        immediateResponse[p] = false;
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

                    //Increase value in allocation matrix and decrease vector (because the values in the request matrix here are negative)
                    alloMtx[p][r] += resourceTbl->reqMtx[p][r];
                    alloVec[r] -= resourceTbl->reqMtx[p][r];

                    //Log to file
                    if (lineCount < 100000) {
                        fprintf(file, "\tResources released: R%i:%i\n", r, abs(resourceTbl->reqMtx[p][r]));
                        ++lineCount;
                    }

                    //Reset Blocked queue to check if their resources can be granted
                    for (int b = 0; b < 18; b++) {
                        blockedQueue[b] = 0;
                    }

                    //Reset value in request matrix
                    //printf("\tThe INCREASE value at [%i][%i] was %i at clock time %li:%09li\n", p, r, resourceTbl->reqMtx[p][r], (long)*sharedSecs, (long)*sharedNS);
                    resourceTbl->reqMtx[p][r] = 0;
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

        //If all processes have been terminated and new processes cannot be created, end program
        for (int e = 0; e < 18; e++) {
            if (resourceTbl->pidArray[e] > 0) {
                endCheck = 0;
                break;
            } else {
                endCheck = 1;
            }
        }

        if (fortyProcs == 40 && endCheck == 1) {
            handle_sig(2);
        }

        //Create child if able
        if (fortyProcs < 40 && maxProcsHit == 0 && (((*sharedSecs * BILLION) + *sharedNS) > ((initialTimeSecs * BILLION) + initialTimeNS + randomTimeNS))) {
            //Add time to the clock
            *sharedNS += 500000; //0.5 milliseconds
            if (*sharedNS >= BILLION) {
                *sharedSecs += 1;
                *sharedNS -= BILLION;
            }
            
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

            //Add to total process count
            ++fortyProcs;

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
                //Record run
                statistics->deadlockRuns += 1;

                if (deadlock(10, 18, alloMtx, alloVec) == true) {
                    //If there are no processes in Blocked queue, escape
                    for (int b = 0; b < 18; b++) {
                        if (blockedQueue[b] < 0) {
                            deadlockEscape = 0;
                            break;
                        } else {
                            deadlockEscape = 1;
                        }
                    }
                    if (deadlockEscape == 1) {
                        break;
                    }

                    //Log to file
                    if (lineCount < 100000) {
                        fprintf(file, "DEADLOCK DETECTED: Processes ");
                        for (int i = 0; i < 18; i++) {
                            if (blockedQueue[i] < 0) {
                                fprintf(file, "P%i ", i);

                                //Add each detection to stats, one time
                                if (collectCheck == 0) {
                                    statistics->deadlockConsiderations += 1;
                                }
                            }
                        }
                        collectCheck = 1;
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

                            //Kill process and clear PID
                            kill(resourceTbl->pidArray[t], SIGTERM);
                            resourceTbl->pidArray[t] = 0;

                            //Log resources
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

                            //Reset values in request matrix
                            for (int v = 0; v < 10; v++) {
                                resourceTbl->reqMtx[t][v] = 0;
                            }

                            //Remove from blocked queue
                            blockedQueue[t] = 0;
                            deadlockClear = 1;

                            //Record termination
                            statistics->deadlockTerminations += 1;

                            break;
                        }
                    }
                } else {
                    //Log to file
                    if (lineCount < 100000) {
                        fprintf(file, "\tNo deadlock detected\n");
                        ++lineCount;
                    }
                    break;
                }
            }

            collectCheck = 0;

            //If deadlocks were detected, Reset Blocked queue
            if (deadlockClear == 1) {
                for (int b = 0; b < 18; b++) {
                    blockedQueue[b] = 0;
                }

                deadlockClear = 0;
            }
            
            //Reset switch
            deadlockSwitch = 1;
        }

        /********************************************************************************************************************
        Empty the Blocked queue if all processes are in it
        *********************************************************************************************************************/
        dumpCheck = true;
        for (int z = 0; z < 18; z++) {
            if (resourceTbl->pidArray[z] > 0) {
                if (blockedQueue[z] < 0) {
                    continue;
                } else {
                    dumpCheck = false;
                    break;
                }
            }
        }

        if (dumpCheck == true) {
            for (int b = 0; b < 18; b++) {
                blockedQueue[b] = 0;
            }
        }

        /********************************************************************************************************************
        Print table every 20 granted requests
        *********************************************************************************************************************/
        if (VERBOSE == 1) {
            if (lineCount < 100000 && requestCount >= 20) {
                fprintf(file, "RESOURCE VECTOR: ");
                for (int a = 0; a < 10; a++) {
                    fprintf(file, "\t%i ", resourceTbl->rsrcVec[a]);
                }
                fprintf(file, "\n");
            }

            if (lineCount < 100000 && requestCount >= 20) {
                fprintf(file, "ALLOCATION VECTOR: ");
                for (int a = 0; a < 10; a++) {
                    fprintf(file, "\t%i ", alloVec[a]);
                }
                fprintf(file, "\n");
            }

            if (lineCount < 100000) {
                if (requestCount >= 20) {
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
    }

    /**********************************************************************************
   
    End doing things here

    ***********************************************************************************/

    return 0;
}