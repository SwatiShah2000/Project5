#ifndef RESOURCE_H
#define RESOURCE_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

#define SHM_KEY 0x1234
#define MSG_KEY 0x5678

#define MAX_PROC 18        // Maximum number of processes
#define MAX_RESOURCES 5    // Number of resource types
#define MAX_INSTANCES 10   // Instances per resource type
#define REQUEST_BOUND 250000000 // 250ms in nanoseconds
#define TERMINATE_CHECK 250000000 // 250ms in nanoseconds
#define MAX_LINES 10000    // Maximum lines in log file
#define MIN_PROC_TIME 1000000000 // Minimum process runtime (1s)

// Message types
#define REQUEST_MSG 1
#define GRANT_MSG 2
#define RELEASE_MSG 3
#define TERMINATE_MSG 4

// Process states
#define UNUSED 0
#define READY 1
#define BLOCKED 2
#define TERMINATED 3

// Clock structure
typedef struct {
    unsigned int seconds;
    unsigned int nanoseconds;
} SystemClock;

// Process control block
typedef struct {
    int pid;                            // Process ID
    int local_pid;                      // Local process ID in our system
    int state;                          // Process state (UNUSED, READY, BLOCKED, TERMINATED)
    unsigned int startTime[2];          // Start time [sec, ns]
    unsigned int blockedTime[2];        // Time when process got blocked
    int allocation[MAX_RESOURCES];      // Resources allocated to this process
    int request[MAX_RESOURCES];         // Resources requested by this process
} PCB;

// Resource descriptor
typedef struct {
    int available[MAX_RESOURCES];                  // Available instances of each resource
    int allocated[MAX_PROC][MAX_RESOURCES];        // Resources allocated to each process
    int request[MAX_PROC][MAX_RESOURCES];          // Resources requested by each process
} ResourceDescriptor;

// Message structure for resource requests/grants
typedef struct {
    long mtype;            // Message type
    int pid;               // Process PID
    int local_pid;         // Local process ID
    int resource_id;       // Resource ID
    int request_action;    // 1 for request, 0 for release
    int quantity;          // Quantity requested/released
} Message;

// Shared memory structure
typedef struct {
    SystemClock clock;
    PCB process_table[MAX_PROC];
    ResourceDescriptor resources;
    int active_procs;      // Number of active processes
} SharedMemory;

// Add time to the system clock
void addTime(SystemClock *clock, unsigned int sec, unsigned int ns);

// Compare two times
int compareTime(unsigned int sec1, unsigned int ns1, unsigned int sec2, unsigned int ns2);

// Get the elapsed time from start to end
void getElapsedTime(unsigned int start_sec, unsigned int start_ns, 
                   unsigned int end_sec, unsigned int end_ns,
                   unsigned int *elapsed_sec, unsigned int *elapsed_ns);

// Check if deadlock exists and return list of deadlocked processes
int detectDeadlock(ResourceDescriptor *rd, PCB *process_table, int active_procs, int *deadlocked);

// Function prototypes for oss
void init_shared_memory(SharedMemory *shm);
void cleanup(int shmid, int msgid);
void signal_handler(int sig);

#endif
