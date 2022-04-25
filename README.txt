How to Run:
1. Navigate to project folder
2. Run "make"
3. Invoke program using "./oss"
4. Examine "LOGFile.txt" for run output

Git Repository: https://github.com/Netsaken/UMSL-CS-4760-Project-5

Notes:
- Deadlock Detection Algorithm: Adapted from the one in our class slides.
- Deadlock Recovery Method: Kills the first process in the waiting (Blocked) queue with SIGTERM. If the deadlock
isn't resolved, kill the second process in the waiting queue, then the third and so on.
- Notifications between OSS and User_proc is done by reading the request matrix (reqMtx). User_proc puts
a number in the matrix and the OSS reads it. If it's positive, OSS treats it as a resource request. If
it's negative, then it's a resource release.
- Additionally, using the request matrix to send messages of self-termination. If the value inside
the matrix is "-30", the OSS will remove that process's values from the relevant tables while the process
handles its own termination.
- Statistics are printed at the end of the LOGFile.

Problems:
- The shared clock is not accessed using a semaphore. Not sure that was required.
- Program frequently runs into an issue where all processes are stuck in the Blocked queue, but
deadlock detection doesn't read a deadlock. I'm not sure if that's an error in the deadlock detection.
- I tried to fix that by emptying the Blocked queue manually if everything wound up inside
of it. However, the program still frequently gets into a situation where the last process(es) alive keep 
requesting resources that they already own and for some reason won't release or terminate through RNG.
- Verbose option not fully implemented. If turned off (by setting the #defined VERBOSE to 0), it will only
stop the table from printing.