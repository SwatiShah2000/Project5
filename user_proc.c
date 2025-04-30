#include "resource.h"
#include "utils.h"

// Global variables for cleanup
int g_shmid = -1;
int g_msgid = -1;

// Change this function to match the declaration in resource.h
void cleanup(int shmid, int msgid) {
    // Detach from shared memory
    if (shmid != -1) {
        shmdt(NULL);
    }
    // You can add message queue cleanup if needed
}

void signal_handler(int sig) {
    printf("Process received signal %d. Cleaning up and terminating...\n", sig);
    cleanup(g_shmid, g_msgid);  // Pass the parameters here
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <local_pid>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int local_pid = atoi(argv[1]);

    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Get shared memory segment
    g_shmid = shmget(SHM_KEY, sizeof(SharedMemory), 0666);
    if (g_shmid == -1) {
        perror("shmget failed");
        exit(EXIT_FAILURE);
    }

    // Attach to shared memory
    SharedMemory *shm = (SharedMemory *) shmat(g_shmid, NULL, 0);
    if (shm == (SharedMemory *) -1) {
        perror("shmat failed");
        cleanup(g_shmid, g_msgid);  // Pass the parameters here
        exit(EXIT_FAILURE);
    }
    // Get message queue
    g_msgid = msgget(MSG_KEY, 0666);
    if (g_msgid == -1) {
        perror("msgget failed");
        cleanup(g_shmid, g_msgid);  // Pass the parameters here
        exit(EXIT_FAILURE);
    }

    // Initialize random number generator
    srand(getpid());

    // Set up resource tracking
    int resources_held[MAX_RESOURCES] = {0};
    int total_resources_held = 0;

    // Record start time
    unsigned int start_sec = shm->clock.seconds;
    unsigned int start_ns = shm->clock.nanoseconds;

    // Parameters for tuning behavior
    const double request_probability = 0.85;  // Higher probability to request than release

    // Next event times
    unsigned int next_request_sec = start_sec;
    unsigned int next_request_ns = start_ns + (rand() % REQUEST_BOUND);
    if (next_request_ns >= 1000000000) {
        next_request_sec++;
        next_request_ns -= 1000000000;
    }

    unsigned int next_check_sec = start_sec;
    unsigned int next_check_ns = start_ns + TERMINATE_CHECK;
    if (next_check_ns >= 1000000000) {
        next_check_sec++;
        next_check_ns -= 1000000000;
    }
    // Main process loop
    while (1) {
        // Check if it's time to request/release resources
        if (compareTime(shm->clock.seconds, shm->clock.nanoseconds, next_request_sec, next_request_ns) >= 0) {
            // Decide whether to request or release
            int action;
            if (total_resources_held == 0) {
                action = 1;  // Must request if nothing is held
            } else if ((rand() / (double)RAND_MAX) < request_probability) {
                action = 1;  // Request
            } else {
                action = 0;  // Release
            }

            if (action == 1) {  // Request
                // Choose a random resource to request
                int resource_id = rand() % MAX_RESOURCES;
                int quantity = 1;  // Always request 1 instance

                // Ensure total requests + allocations don't exceed max instances
                if (resources_held[resource_id] < MAX_INSTANCES) {
                    // Create and send request message
                    Message msg;
                    msg.mtype = REQUEST_MSG;
                    msg.pid = getpid();
                    msg.local_pid = local_pid;
                    msg.resource_id = resource_id;
                    msg.request_action = 1;
                    msg.quantity = quantity;

                    if (msgsnd(g_msgid, &msg, sizeof(Message) - sizeof(long), 0) == -1) {
                        perror("msgsnd failed");
                        cleanup(g_shmid, g_msgid);  // Pass the parameters here
                        exit(EXIT_FAILURE);
                    }

                    // Wait for grant message
                    Message response;
                    if (msgrcv(g_msgid, &response, sizeof(Message) - sizeof(long), GRANT_MSG, 0) == -1) {
                        perror("msgrcv failed");
                        cleanup(g_shmid, g_msgid);  // Pass the parameters here
                        exit(EXIT_FAILURE);
                    }
                    // Resource granted, update local tracking
                    resources_held[resource_id] += quantity;
                    total_resources_held += quantity;
                }
            } else {  // Release
                // Find a resource that we hold
                int resource_id = -1;
                for (int i = 0; i < MAX_RESOURCES; i++) {
                    if (resources_held[i] > 0) {
                        resource_id = i;
                        break;
                    }
                }

                if (resource_id != -1) {
                    int quantity = resources_held[resource_id];

                    // Create and send release message
                    Message msg;
                    msg.mtype = RELEASE_MSG;
                    msg.pid = getpid();
                    msg.local_pid = local_pid;
                    msg.resource_id = resource_id;
                    msg.request_action = 0;
                    msg.quantity = quantity;

                    if (msgsnd(g_msgid, &msg, sizeof(Message) - sizeof(long), 0) == -1) {
                        perror("msgsnd failed");
                        cleanup(g_shmid, g_msgid);  // Pass the parameters here
                        exit(EXIT_FAILURE);
                    }

                    // Update local tracking
                    resources_held[resource_id] = 0;
                    total_resources_held -= quantity;
                }
            }

            // Set next request time
            unsigned int delay = rand() % REQUEST_BOUND;
            next_request_ns = shm->clock.nanoseconds + delay;
            next_request_sec = shm->clock.seconds;
            if (next_request_ns >= 1000000000) {
                next_request_sec++;
                next_request_ns -= 1000000000;
            }
        }

        // Check if process should terminate (every 250ms)
        if (compareTime(shm->clock.seconds, shm->clock.nanoseconds, next_check_sec, next_check_ns) >= 0) {
            // Calculate elapsed time
            unsigned int elapsed_sec, elapsed_ns;
            getElapsedTime(start_sec, start_ns, shm->clock.seconds, shm->clock.nanoseconds, &elapsed_sec, &elapsed_ns);

            // Check if process has run for at least 1 second
            if (elapsed_sec >= 1 || (elapsed_sec == 0 && elapsed_ns >= MIN_PROC_TIME)) {
                // Decide whether to terminate (10% chance)
                if ((rand() / (double)RAND_MAX) < 0.10) {
                    // Release all resources before terminating
                    for (int i = 0; i < MAX_RESOURCES; i++) {
                        if (resources_held[i] > 0) {
                            Message msg;
                            msg.mtype = RELEASE_MSG;
                            msg.pid = getpid();
                            msg.local_pid = local_pid;
                            msg.resource_id = i;
                            msg.request_action = 0;
                            msg.quantity = resources_held[i];

                            if (msgsnd(g_msgid, &msg, sizeof(Message) - sizeof(long), 0) == -1) {
                                perror("msgsnd failed");
                            }

                            resources_held[i] = 0;
                        }
                    }

                    // Send termination message
                    Message msg;
                    msg.mtype = TERMINATE_MSG;
                    msg.pid = getpid();
                    msg.local_pid = local_pid;

                    if (msgsnd(g_msgid, &msg, sizeof(Message) - sizeof(long), 0) == -1) {
                        perror("msgsnd failed");
                    }

                    // Cleanup and exit
                    cleanup(g_shmid, g_msgid);  // Pass the parameters here
                    exit(EXIT_SUCCESS);
                }
            }
            // Set next check time
            next_check_ns = shm->clock.nanoseconds + TERMINATE_CHECK;
            next_check_sec = shm->clock.seconds;
            if (next_check_ns >= 1000000000) {
                next_check_sec++;
                next_check_ns -= 1000000000;
            }
        }
    }

    // Cleanup (should never reach here)
    cleanup(g_shmid, g_msgid);  // Pass the parameters here
    return EXIT_SUCCESS;
}
