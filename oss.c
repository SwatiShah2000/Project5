/*
 * Operating System Simulator (OSS)
 * Resource Management with Deadlock Detection
 * 
 * This program simulates an operating system resource manager
 * that handles resource allocation using deadlock detection and recovery.
 */

#include "resource.h" // Make sure this contains your SystemClock and other definitions
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>

// Shared memory structure
typedef struct {
    SystemClock clock;
    int resources[MAX_RESOURCES][MAX_INSTANCES]; // 0 = free, pid = allocated to process
} SharedMemory;

// Process table entry
typedef struct {
    pid_t pid;           // 0 if unused
    int state;           // UNUSED, READY, BLOCKED, TERMINATED
    int index;           // Process index for display
    SystemClock startTime;
    int allocated[MAX_RESOURCES];  // Number of instances allocated
    int requested[MAX_RESOURCES];  // Number of instances requested but not granted
    int waiting_for;    // Resource index the process is waiting for (-1 if not waiting)
} PCB;

// Resource descriptor
typedef struct {
    int total;           // Total instances
    int available;       // Available instances
} ResourceDescriptor;

// Message structure
typedef struct {
    long mtype;         // Message type
    int sender_id;      // Process index
    int resource_id;    // Resource index
    int num_instances;  // Number of instances
} Message;

// Global variables
SharedMemory *shm = NULL;
PCB processTable[MAX_PROC];
ResourceDescriptor resourceDescriptors[MAX_RESOURCES];
int shmid = -1;
int msqid = -1;
FILE *logfile = NULL;
int verbose = 1;
int line_count = 0;
int total_processes = 0;
int active_processes = 0;
int max_processes = 100;  // Default, can be set via command line
int simul_processes = 18; // Default, can be set via command line
int interval_ms = 1000;   // Default, can be set via command line
char *log_filename = "oss.log"; // Default log file name
time_t start_time;

// Statistics
int requests_granted_immediately = 0;
int requests_granted_after_wait = 0;
int processes_terminated_normally = 0;
int processes_terminated_by_deadlock = 0;
int deadlock_runs = 0;
int total_deadlocked = 0;

// Function prototypes
void initSharedMemory();
void initMessageQueue();
void initProcessTable();
void initResourceDescriptors();
void cleanup();
void printHelp();
void processArgs(int argc, char *argv[]);
void handleSignal(int sig);
int launchNewProcess();
int findNextFreeSlot();
int findProcessByPid(pid_t pid);
void releaseAllResources(int proc_idx);
void printResourceTable();
void checkWaitingRequests();
void checkForMessages();
int detectDeadlock(int *deadlocked);
void resolveDeadlock(int *deadlocked);
int shouldLaunchProcess();
int shouldPrintResourceTable();
int shouldRunDeadlockDetection();
int shouldTerminate();
void logMessage(const char *format, ...);

int main(int argc, char *argv[]) {
    // Parse command line arguments
    processArgs(argc, argv);
    
    // Set up signal handlers
    signal(SIGINT, handleSignal);
    signal(SIGALRM, handleSignal);
    signal(SIGTERM, handleSignal);
    
    // Open log file
    logfile = fopen(log_filename, "w");
    if (!logfile) {
        perror("Failed to open log file");
        exit(EXIT_FAILURE);
    }
    
    // Initialize resources
    initSharedMemory();
    initMessageQueue();
    initProcessTable();
    initResourceDescriptors();
    
    // Seed random number generator
    srand(time(NULL));
    
    // Record start time
    start_time = time(NULL);
    
    // Set alarm for 5 seconds (real time limit)
    alarm(5);
    
    logMessage("OSS: Resource Management System Started\n");
    
    // Variables for tracking time
    unsigned int last_resource_table_time = 0;
    unsigned int last_deadlock_check_time = 0;
    
    // Create initial process
    printf("DEBUG: Attempting to create initial process\n");
    launchNewProcess();
    
    // Main simulation loop
    int still_running = 1;
    while (still_running) {
        // Increment the clock
        unsigned int ns_increment = (rand() % 1000000) + 10000; // Random 10k-1M ns
        shm->clock.nanoseconds += ns_increment;
        if (shm->clock.nanoseconds >= 1000000000) {
            shm->clock.nanoseconds -= 1000000000;
            shm->clock.seconds++;
        }
        
        // Check for terminated children
        pid_t terminated_pid;
        while ((terminated_pid = waitpid(-1, NULL, WNOHANG)) > 0) {
            int proc_idx = findProcessByPid(terminated_pid);
            if (proc_idx >= 0) {
                logMessage("OSS: Process P%d terminated at time %u:%u\n", 
                          proc_idx, shm->clock.seconds, shm->clock.nanoseconds);
                          
                // Release its resources
                releaseAllResources(proc_idx);
                
                // Mark process table entry as unused
                processTable[proc_idx].pid = 0;
                processTable[proc_idx].state = UNUSED;
                active_processes--;
                processes_terminated_normally++;
                
                printf("DEBUG: Process P%d terminated\n", proc_idx);
            }
        }
        
        // Launch a new process if appropriate
        if (shouldLaunchProcess()) {
            printf("DEBUG: Attempting to launch a new process\n");
            launchNewProcess();
        }
        
        // Check if we can grant any waiting requests
        checkWaitingRequests();
        
        // Check for messages from processes
        checkForMessages();
        
        // Print resource table every half second of simulated time
        if (shm->clock.seconds > last_resource_table_time || 
            (shm->clock.seconds == last_resource_table_time && 
             shm->clock.nanoseconds >= 500000000 && 
             last_resource_table_time < shm->clock.nanoseconds)) {
            
            last_resource_table_time = shm->clock.seconds;
            printResourceTable();
        }
        
        // Run deadlock detection every second of simulated time
        if (shm->clock.seconds > last_deadlock_check_time) {
            last_deadlock_check_time = shm->clock.seconds;
            
            logMessage("Master running deadlock detection at time %u:%u: ",
                     shm->clock.seconds, shm->clock.nanoseconds);
            
            int deadlocked[MAX_PROC] = {0};
            int num_deadlocked = detectDeadlock(deadlocked);
            deadlock_runs++;
            
            if (num_deadlocked > 0) {
                total_deadlocked += num_deadlocked;
                
                // List deadlocked processes
                logMessage("Processes ");
                for (int i = 0; i < MAX_PROC; i++) {
                    if (deadlocked[i])
                        logMessage("P%d, ", i);
                }
                logMessage("deadlocked\n");
                
                // Resolve deadlock
                resolveDeadlock(deadlocked);
            } else {
                logMessage("No deadlocks detected\n");
            }
        }
        
        // Check if we should terminate
        if (shouldTerminate()) {
            still_running = 0;
        }
    }
    
    // Print final statistics
    logMessage("\n--- Final Statistics ---\n");
    logMessage("Requests granted immediately: %d\n", requests_granted_immediately);
    logMessage("Requests granted after waiting: %d\n", requests_granted_after_wait);
    logMessage("Processes terminated normally: %d\n", processes_terminated_normally);
    logMessage("Processes terminated by deadlock: %d\n", processes_terminated_by_deadlock);
    logMessage("Deadlock detection runs: %d\n", deadlock_runs);
    
    if (deadlock_runs > 0) {
        logMessage("Average processes in deadlock per detection: %.2f\n", 
                 (float)total_deadlocked / deadlock_runs);
    }
    
    if (processes_terminated_by_deadlock > 0) {
        logMessage("Percentage of deadlocked processes terminated: %.2f%%\n", 
                 (float)processes_terminated_by_deadlock / (processes_terminated_by_deadlock + processes_terminated_normally) * 100);
    }
    
    // Clean up
    cleanup();
    
    return EXIT_SUCCESS;
}

// Initialize shared memory
void initSharedMemory() {
    // Create shared memory segment
    shmid = shmget(SHM_KEY, sizeof(SharedMemory), IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("shmget failed");
        cleanup();
        exit(EXIT_FAILURE);
    }
    
    // Attach to shared memory
    shm = (SharedMemory *)shmat(shmid, NULL, 0);
    if (shm == (void *)-1) {
        perror("shmat failed");
        cleanup();
        exit(EXIT_FAILURE);
    }
    
    // Initialize clock
    shm->clock.seconds = 0;
    shm->clock.nanoseconds = 0;
    
    // Initialize resources array
    for (int i = 0; i < MAX_RESOURCES; i++) {
        for (int j = 0; j < MAX_INSTANCES; j++) {
            shm->resources[i][j] = 0; // 0 means free
        }
    }
}

// Initialize message queue
void initMessageQueue() {
    msqid = msgget(MSG_KEY, IPC_CREAT | 0666);
    if (msqid == -1) {
        perror("msgget failed");
        cleanup();
        exit(EXIT_FAILURE);
    }
}

// Initialize process table
void initProcessTable() {
    for (int i = 0; i < MAX_PROC; i++) {
        processTable[i].pid = 0;
        processTable[i].state = UNUSED;
        processTable[i].index = i;
        processTable[i].startTime.seconds = 0;
        processTable[i].startTime.nanoseconds = 0;
        processTable[i].waiting_for = -1;
        
        for (int j = 0; j < MAX_RESOURCES; j++) {
            processTable[i].allocated[j] = 0;
            processTable[i].requested[j] = 0;
        }
    }
}

// Initialize resource descriptors
void initResourceDescriptors() {
    for (int i = 0; i < MAX_RESOURCES; i++) {
        resourceDescriptors[i].total = MAX_INSTANCES;
        resourceDescriptors[i].available = MAX_INSTANCES;
    }
}

// Clean up resources
void cleanup() {
    // Kill all child processes
    for (int i = 0; i < MAX_PROC; i++) {
        if (processTable[i].pid > 0) {
            kill(processTable[i].pid, SIGTERM);
        }
    }
    
    // Wait for all children to terminate
    while (waitpid(-1, NULL, WNOHANG) > 0);
    
    // Close log file
    if (logfile) {
        fclose(logfile);
        logfile = NULL;
    }
    
    // Detach from shared memory
    if (shm != NULL && shm != (void *)-1) {
        shmdt(shm);
    }
    
    // Remove shared memory segment
    if (shmid != -1) {
        shmctl(shmid, IPC_RMID, NULL);
    }
    
    // Remove message queue
    if (msqid != -1) {
        msgctl(msqid, IPC_RMID, NULL);
    }
}

// Print command line help
void printHelp() {
    printf("Usage: oss [-h] [-n proc] [-s simul] [-i interval] [-f logfile]\n");
    printf("  -h             : Display this help message\n");
    printf("  -n proc        : Total number of processes to create\n");
    printf("  -s simul       : Maximum number of processes running simultaneously\n");
    printf("  -i interval    : Interval in milliseconds between process creation\n");
    printf("  -f logfile     : Log file name\n");
}

// Process command line arguments
void processArgs(int argc, char *argv[]) {
    int opt;
    
    while ((opt = getopt(argc, argv, "hn:s:i:f:")) != -1) {
        switch (opt) {
            case 'h':
                printHelp();
                exit(EXIT_SUCCESS);
                break;
            case 'n':
                max_processes = atoi(optarg);
                if (max_processes <= 0) {
                    fprintf(stderr, "Invalid number of processes\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 's':
                simul_processes = atoi(optarg);
                if (simul_processes <= 0 || simul_processes > MAX_PROC) {
                    fprintf(stderr, "Invalid number of simultaneous processes (max %d)\n", MAX_PROC);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'i':
                interval_ms = atoi(optarg);
                if (interval_ms < 0) {
                    fprintf(stderr, "Invalid interval\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 'f':
                log_filename = optarg;
                break;
            default:
                fprintf(stderr, "Invalid option\n");
                printHelp();
                exit(EXIT_FAILURE);
        }
    }
}

// Signal handler
void handleSignal(int sig) {
    if (sig == SIGINT || sig == SIGTERM || sig == SIGALRM) {
        printf("\nReceived signal %d. Cleaning up and terminating...\n", sig);
        cleanup();
        exit(EXIT_SUCCESS);
    }
}

// Launch a new process
int launchNewProcess() {
    if (active_processes >= simul_processes || total_processes >= max_processes) {
        return -1;
    }
    
    int proc_idx = findNextFreeSlot();
    if (proc_idx == -1) {
        return -1; // No free slot
    }
    
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork failed");
        return -1;
    } else if (pid == 0) {
        // Child process
        char proc_idx_str[10];
        sprintf(proc_idx_str, "%d", proc_idx);
        
        execl("./user_proc", "user_proc", proc_idx_str, NULL);
        
        // If execl fails
        perror("execl failed");
        exit(EXIT_FAILURE);
    } else {
        // Parent process
        processTable[proc_idx].pid = pid;
        processTable[proc_idx].state = READY;
        processTable[proc_idx].startTime = shm->clock;
        processTable[proc_idx].waiting_for = -1;
        
        // Initialize allocation and request arrays
        for (int i = 0; i < MAX_RESOURCES; i++) {
            processTable[proc_idx].allocated[i] = 0;
            processTable[proc_idx].requested[i] = 0;
        }
        
        total_processes++;
        active_processes++;
        
        logMessage("OSS: Process P%d created at time %u:%u\n", 
                 proc_idx, shm->clock.seconds, shm->clock.nanoseconds);
        
        // Debug output
        printf("DEBUG: Created process P%d (PID: %d)\n", proc_idx, pid);
        
        return proc_idx;
    }
}

// Find next free slot in process table
int findNextFreeSlot() {
    for (int i = 0; i < MAX_PROC; i++) {
        if (processTable[i].pid == 0) {
            return i;
        }
    }
    return -1; // No free slot
}

// Find process index by PID
int findProcessByPid(pid_t pid) {
    for (int i = 0; i < MAX_PROC; i++) {
        if (processTable[i].pid == pid) {
            return i;
        }
    }
    return -1; // Not found
}

// Release all resources allocated to a process
void releaseAllResources(int proc_idx) {
    if (proc_idx < 0 || proc_idx >= MAX_PROC) {
        return;
    }
    
    logMessage("Resources released: ");
    int released = 0;
    
    for (int i = 0; i < MAX_RESOURCES; i++) {
        if (processTable[proc_idx].allocated[i] > 0) {
            logMessage("R%d:%d ", i, processTable[proc_idx].allocated[i]);
            
            // Update available resources
            resourceDescriptors[i].available += processTable[proc_idx].allocated[i];
            processTable[proc_idx].allocated[i] = 0;
            released = 1;
        }
    }
    
    if (!released) {
        logMessage("none");
    }
    
    logMessage("\n");
}

// Print resource table
void printResourceTable() {
    if (line_count >= MAX_LINES) {
        return;
    }
    
    logMessage("\nCurrent Resource Table (Time %u:%u):\n", 
             shm->clock.seconds, shm->clock.nanoseconds);
    
    // Print header
    logMessage("   |");
    for (int i = 0; i < MAX_RESOURCES; i++) {
        logMessage(" R%d", i);
    }
    logMessage("\n---+");
    for (int i = 0; i < MAX_RESOURCES; i++) {
        logMessage("---");
    }
    logMessage("\n");
    
    // Print process allocations
    int active_procs = 0;
    for (int i = 0; i < MAX_PROC; i++) {
        if (processTable[i].pid > 0) {
            logMessage("P%d |", i);
            for (int j = 0; j < MAX_RESOURCES; j++) {
                logMessage(" %d ", processTable[i].allocated[j]);
            }
            logMessage("\n");
            active_procs++;
        }
    }
    
    // If no active processes, say so
    if (active_procs == 0) {
        logMessage("No active processes\n");
    }
    
    // Print available resources
    logMessage("AV |");
    for (int i = 0; i < MAX_RESOURCES; i++) {
        logMessage(" %d ", resourceDescriptors[i].available);
    }
    logMessage("\n");
    
    // Debug output
    printf("DEBUG: Active processes: %d, Total processes: %d\n", 
           active_processes, total_processes);
}

// Check if any waiting requests can be granted
void checkWaitingRequests() {
    // Check each process
    for (int i = 0; i < MAX_PROC; i++) {
        if (processTable[i].state == BLOCKED && processTable[i].waiting_for >= 0) {
            int res_id = processTable[i].waiting_for;
            
            // Check if the resource is available
            if (resourceDescriptors[res_id].available > 0) {
                // Grant the resource
                resourceDescriptors[res_id].available--;
                processTable[i].allocated[res_id]++;
                processTable[i].waiting_for = -1;
                processTable[i].state = READY;
                
                // Send grant message
                Message response;
                response.mtype = GRANT_MSG;
                response.sender_id = 0; // from master
                response.resource_id = res_id;
                response.num_instances = 1;
                
                if (msgsnd(msqid, &response, sizeof(response) - sizeof(long), 0) == -1) {
                    perror("msgsnd failed");
                }
                
                logMessage("Master granting P%d request R%d at time %u:%u (from wait queue)\n",
                         i, res_id, shm->clock.seconds, shm->clock.nanoseconds);
                
                requests_granted_after_wait++;
            }
        }
    }
}

// Check for messages from processes
void checkForMessages() {
    Message msg;
    
    // Non-blocking message receive
    while (msgrcv(msqid, &msg, sizeof(msg) - sizeof(long), 0, IPC_NOWAIT) > 0) {
        int proc_idx = msg.sender_id;
        int res_id = msg.resource_id;
        
        printf("DEBUG: Received message type %ld from P%d about R%d\n", 
               msg.mtype, proc_idx, res_id);
        
        switch (msg.mtype) {
            case REQUEST_MSG:
                logMessage("Master has detected Process P%d requesting R%d at time %u:%u\n",
                         proc_idx, res_id, shm->clock.seconds, shm->clock.nanoseconds);
                
                // Check if resource is available
                if (resourceDescriptors[res_id].available > 0) {
                    // Grant the resource
                    resourceDescriptors[res_id].available--;
                    processTable[proc_idx].allocated[res_id]++;
                    
                    // Send grant message
                    Message response;
                    response.mtype = GRANT_MSG;
                    response.sender_id = 0; // from master
                    response.resource_id = res_id;
                    response.num_instances = 1;
                    
                    if (msgsnd(msqid, &response, sizeof(response) - sizeof(long), 0) == -1) {
                        perror("msgsnd failed");
                    }
                    
                    logMessage("Master granting P%d request R%d at time %u:%u\n",
                             proc_idx, res_id, shm->clock.seconds, shm->clock.nanoseconds);
                    
                    requests_granted_immediately++;
                } else {
                    // Add to wait queue
                    processTable[proc_idx].state = BLOCKED;
                    processTable[proc_idx].waiting_for = res_id;
                    processTable[proc_idx].requested[res_id]++;
                    
                    logMessage("Master: no instances of R%d available, P%d added to wait queue at time %u:%u\n",
                             res_id, proc_idx, shm->clock.seconds, shm->clock.nanoseconds);
                }
                break;
                
            case RELEASE_MSG:
                logMessage("Master has acknowledged Process P%d releasing R%d at time %u:%u\n",
                         proc_idx, res_id, shm->clock.seconds, shm->clock.nanoseconds);
                
                // Check that process actually has the resource
                if (processTable[proc_idx].allocated[res_id] > 0) {
                    // Release the resource
                    processTable[proc_idx].allocated[res_id]--;
                    resourceDescriptors[res_id].available++;
                    
                    logMessage("Resources released: R%d:%d\n", res_id, 1);
                }
                break;
                
            case TERMINATE_MSG:
                // Process is terminating, but we'll handle its resources when we get the SIGCHLD
                break;
        }
    }
    
    // Reset errno set by msgrcv when queue is empty
    if (errno == ENOMSG) {
        errno = 0;
    }
}

// Deadlock detection algorithm
int detectDeadlock(int *deadlocked) {
    int work[MAX_RESOURCES];
    int finish[MAX_PROC];
    int num_deadlocked = 0;
    
    // Initialize work array with available resources
    for (int i = 0; i < MAX_RESOURCES; i++) {
        work[i] = resourceDescriptors[i].available;
    }
    
    // Initialize finish array
    for (int i = 0; i < MAX_PROC; i++) {
        finish[i] = (processTable[i].pid == 0); // True if process doesn't exist
    }
    
    // Find a process that can finish
    int found;
    do {
        found = 0;
        for (int i = 0; i < MAX_PROC; i++) {
            if (processTable[i].pid > 0 && !finish[i]) {
                // Check if process can finish with available resources
                int can_finish = 1;
                for (int j = 0; j < MAX_RESOURCES; j++) {
                    if (processTable[i].requested[j] > work[j]) {
                        can_finish = 0;
                        break;
                    }
                }
                
                if (can_finish) {
                    // Process can finish, add its resources to work
                    for (int j = 0; j < MAX_RESOURCES; j++) {
                        work[j] += processTable[i].allocated[j];
                    }
                    finish[i] = 1;
                    found = 1;
                }
            }
        }
    } while (found);
    
    // Check for deadlocked processes
    for (int i = 0; i < MAX_PROC; i++) {
        if (processTable[i].pid > 0 && !finish[i]) {
            deadlocked[i] = 1;
            num_deadlocked++;
        } else {
            deadlocked[i] = 0;
        }
    }
    
    return num_deadlocked;
}

// Resolve deadlock by terminating a process
void resolveDeadlock(int *deadlocked) {
    // Find a process to terminate - for simplicity, terminate the first deadlocked process
    for (int i = 0; i < MAX_PROC; i++) {
        if (deadlocked[i]) {
            logMessage("Master terminating P%d to remove deadlock\n", i);
            
            // Release its resources
            releaseAllResources(i);
            
            // Kill the process
            kill(processTable[i].pid, SIGTERM);
            
            // Update process table
            processTable[i].pid = 0;
            processTable[i].state = UNUSED;
            active_processes--;
            processes_terminated_by_deadlock++;
            
            // We only terminate one process at a time to minimize interference
            break;
        }
    }
}

// Check if we should launch a new process
int shouldLaunchProcess() {
    // Simple time-based check using real time
    static time_t last_launch_time = 0;
    time_t current_time = time(NULL);
    
    // Only create a process if we have room and enough time has passed
    if (active_processes < simul_processes && total_processes < max_processes) {
        // Convert interval_ms to seconds and check if enough time has passed
        if (current_time - last_launch_time >= (interval_ms / 1000) || last_launch_time == 0) {
            last_launch_time = current_time;
            return 1;
        }
    }
    
    return 0;
}

// Check if we should terminate
int shouldTerminate() {
    // Terminate if we've created enough processes and none are active
    if (total_processes >= max_processes && active_processes == 0) {
        return 1;
    }
    
    // Terminate if we've been running too long (real time)
    if (difftime(time(NULL), start_time) > 5) {
        return 1;
    }
    
    return 0;
}

// Log a message to both stdout and log file
void logMessage(const char *format, ...) {
    va_list args;
    
    if (line_count < MAX_LINES) {
        // Print to stdout
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
        
        // Print to log file
        if (logfile) {
            va_start(args, format);
            vfprintf(logfile, format, args);
            va_end(args);
            fflush(logfile);
        }
        
        line_count++;
    }
}
