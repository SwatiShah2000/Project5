/*
 * User Process
 * Resource Management Simulation
 * 
 * This program simulates a user process that requests and releases resources.
 */

#include "resource.h" // Make sure this contains your SystemClock and other definitions
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <signal.h>
#include <time.h>
#include <string.h>

// Shared memory structure
typedef struct {
    SystemClock clock;
    int resources[MAX_RESOURCES][MAX_INSTANCES]; // 0 = free, pid = allocated to process
} SharedMemory;

// Message structure
typedef struct {
    long mtype;         // Message type
    int sender_id;      // Process index
    int resource_id;    // Resource index
    int num_instances;  // Number of instances
} Message;

// Global variables
SharedMemory *shm = NULL;
int shmid = -1;
int msqid = -1;
int my_idx = -1;
int my_allocated[MAX_RESOURCES];
SystemClock start_time;

// Function prototypes
void cleanup();
void handleSignal(int sig);
void requestResource(int resource_id);
void releaseResource(int resource_id);
int hasResources();
int shouldTerminate();
unsigned int getRandomInRange(unsigned int max);

// Clean up resources
void cleanup() {
    // Detach from shared memory
    if (shm != NULL && shm != (void *)-1) {
        shmdt(shm);
    }
}

// Signal handler
void handleSignal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        printf("Process received signal %d. Cleaning up and terminating...\n", sig);
        cleanup();
        exit(EXIT_SUCCESS);
    }
}

// Request a resource
void requestResource(int resource_id) {
    // Prepare message
    Message msg;
    msg.mtype = REQUEST_MSG;
    msg.sender_id = my_idx;
    msg.resource_id = resource_id;
    msg.num_instances = 1;
    
    // Debug output
    printf("DEBUG: Process P%d requesting resource R%d\n", my_idx, resource_id);
    
    // Send request
    if (msgsnd(msqid, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
        perror("msgsnd failed");
        return;
    }
    
    // Wait for response
    while (1) {
        if (msgrcv(msqid, &msg, sizeof(msg) - sizeof(long), GRANT_MSG, 0) == -1) {
            perror("msgrcv failed");
            return;
        }
        
        // Check if this is our response
        if (msg.sender_id == 0 && msg.resource_id == resource_id) {
            my_allocated[resource_id]++;
            printf("DEBUG: Process P%d granted resource R%d\n", my_idx, resource_id);
            break;
        }
    }
}

// Release a resource
void releaseResource(int resource_id) {
    if (my_allocated[resource_id] <= 0) {
        return; // We don't have this resource
    }
    
    // Prepare message
    Message msg;
    msg.mtype = RELEASE_MSG;
    msg.sender_id = my_idx;
    msg.resource_id = resource_id;
    msg.num_instances = 1;
    
    // Send release message
    if (msgsnd(msqid, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
        perror("msgsnd failed");
        return;
    }
    
    my_allocated[resource_id]--;
}

// Check if we have any resources
int hasResources() {
    for (int i = 0; i < MAX_RESOURCES; i++) {
        if (my_allocated[i] > 0) {
            return 1;
        }
    }
    return 0;
}

// Check if we should terminate
int shouldTerminate() {
    // Calculate elapsed time since process started
    unsigned int sec_elapsed = shm->clock.seconds - start_time.seconds;
    unsigned int ns_elapsed = 0;
    
    if (shm->clock.nanoseconds < start_time.nanoseconds) {
        sec_elapsed--;
        ns_elapsed = 1000000000 + shm->clock.nanoseconds - start_time.nanoseconds;
    } else {
        ns_elapsed = shm->clock.nanoseconds - start_time.nanoseconds;
    }
    
    // Check if we've run for at least 1 second
    if (sec_elapsed >= 1 || (sec_elapsed == 0 && ns_elapsed >= MIN_PROC_TIME)) {
        // Check every 250ms (converted to ns) if we should terminate
        static unsigned int last_check = 0;
        if (ns_elapsed - last_check >= TERMINATE_CHECK || ns_elapsed < last_check) {
            last_check = ns_elapsed;
            
            // 25% chance to terminate
            if (rand() % 100 < 25) {
                return 1;
            }
        }
    }
    
    return 0;
}

// Get random number in range [0, max]
unsigned int getRandomInRange(unsigned int max) {
    return rand() % (max + 1);
}

int main(int argc, char *argv[]) {
    // Set up signal handlers
    signal(SIGINT, handleSignal);
    signal(SIGTERM, handleSignal);
    
    // Check arguments
    if (argc < 2) {
        fprintf(stderr, "Usage: %s process_index\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    // Get process index
    my_idx = atoi(argv[1]);
    
    // Get shared memory
    shmid = shmget(SHM_KEY, sizeof(SharedMemory), 0);
    if (shmid == -1) {
        perror("shmget failed");
        exit(EXIT_FAILURE);
    }
    
    // Attach to shared memory
    shm = (SharedMemory *)shmat(shmid, NULL, 0);
    if (shm == (void *)-1) {
        perror("shmat failed");
        exit(EXIT_FAILURE);
    }
    
    // Get message queue
    msqid = msgget(MSG_KEY, 0);
    if (msqid == -1) {
        perror("msgget failed");
        cleanup();
        exit(EXIT_FAILURE);
    }
    
    // Initialize
    start_time = shm->clock;
    for (int i = 0; i < MAX_RESOURCES; i++) {
        my_allocated[i] = 0;
    }
    
    // Seed random number generator
    srand(time(NULL) + my_idx);
    
    printf("DEBUG: User process P%d started\n", my_idx);
    
    // Main loop
    while (1) {
        // Random chance (10%) to add a short delay
        if (rand() % 100 < 10) {
            usleep(10000);  // 10ms delay
        }
    
        // Check if we should terminate
        if (shouldTerminate()) {
            printf("DEBUG: Process P%d deciding to terminate\n", my_idx);
            
            // Release all resources
            for (int i = 0; i < MAX_RESOURCES; i++) {
                while (my_allocated[i] > 0) {
                    releaseResource(i);
                }
            }
            
            // Send termination message
            Message msg;
            msg.mtype = TERMINATE_MSG;
            msg.sender_id = my_idx;
            
            if (msgsnd(msqid, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
                perror("msgsnd failed");
            }
            
            break;
        }
        
        // Generate random number to decide action
        int action = rand() % 100;
        
        // 80% chance to request, 20% chance to release
        if (action < 80 || !hasResources()) {
            // Request a resource
            int resource_id = rand() % MAX_RESOURCES;
            
            // Make sure we don't request more than maximum
            if (my_allocated[resource_id] < MAX_INSTANCES) {
                requestResource(resource_id);
            }
        } else {
            // Release a resource if we have any
            int resource_id;
            int has_resource = 0;
            
            // Find a resource we have allocated
            do {
                resource_id = rand() % MAX_RESOURCES;
                if (my_allocated[resource_id] > 0) {
                    has_resource = 1;
                }
            } while (!has_resource && hasResources());
            
            if (has_resource) {
                releaseResource(resource_id);
            }
        }
        
        // Sleep for a bit (nanoseconds converted to microseconds)
        usleep(getRandomInRange(REQUEST_BOUND) / 1000);
    }
    
    // Clean up
    cleanup();
    
    return EXIT_SUCCESS;
}
