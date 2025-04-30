/*
 * Resource Management Simulation
 * Header file for shared definitions
 */

#ifndef RESOURCE_H
#define RESOURCE_H

// Shared memory and message queue keys
#define SHM_KEY 0x1234
#define MSG_KEY 0x5678

// Process and resource limits
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

// Function declarations
void addTime(SystemClock *clock, unsigned int sec, unsigned int ns);

#endif

