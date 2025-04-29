#include "resource.h"

// Global variables for cleanup
int g_shmid = -1;
int g_msgid = -1;
FILE *g_logfile = NULL;
int g_lines_written = 0;
int g_verbose = 1;  // Default verbose mode

// Statistics
int requests_granted_immediately = 0;
int requests_granted_after_wait = 0;
int processes_terminated_normally = 0;
int processes_terminated_deadlock = 0;
int deadlock_runs = 0;
int processes_deadlocked_total = 0;

void log_message(FILE *fp, const char *format, ...) {
    if (g_lines_written >= MAX_LINES) return;

    va_list args;
    va_start(args, format);

    vfprintf(fp, format, args);
    vfprintf(stdout, format, args);
    va_end(args);
    g_lines_written++;
}

void print_resource_table(FILE *fp, SharedMemory *shm) {
    if (g_lines_written >= MAX_LINES) return;

    log_message(fp, "\nCurrent Resource Table (Time %u:%u):\n",
                shm->clock.seconds, shm->clock.nanoseconds);

    // Print header
    log_message(fp, "   | ");
    for (int i = 0; i < MAX_RESOURCES; i++) {
        log_message(fp, "R%d ", i);
    }
    log_message(fp, "\n---+");
    for (int i = 0; i < MAX_RESOURCES; i++) {
        log_message(fp, "---");
    }
    log_message(fp, "\n");

    // Print allocated resources for each process
    for (int i = 0; i < MAX_PROC; i++) {
        if (shm->process_table[i].pid > 0) {
            log_message(fp, "P%d | ", i);
            for (int j = 0; j < MAX_RESOURCES; j++) {
                log_message(fp, "%d  ", shm->resources.allocated[i][j]);
            }
            log_message(fp, "\n");
        }
    }

    // Print available resources
    log_message(fp, "AV | ");
    for (int i = 0; i < MAX_RESOURCES; i++) {
        log_message(fp, "%d  ", shm->resources.available[i]);
    }
    log_message(fp, "\n");
}

void init_shared_memory(SharedMemory *shm) {
    // Initialize clock
    shm->clock.seconds = 0;
    shm->clock.nanoseconds = 0;

    // Initialize process table
    for (int i = 0; i < MAX_PROC; i++) {
        shm->process_table[i].pid = 0;
        shm->process_table[i].local_pid = i;
        shm->process_table[i].state = UNUSED;

        for (int j = 0; j < MAX_RESOURCES; j++) {
            shm->process_table[i].allocation[j] = 0;
            shm->process_table[i].request[j] = 0;
        }
    }

    // Initialize resource descriptor
    for (int i = 0; i < MAX_RESOURCES; i++) {
        shm->resources.available[i] = MAX_INSTANCES;

        for (int j = 0; j < MAX_PROC; j++) {
            shm->resources.allocated[j][i] = 0;
            shm->resources.request[j][i] = 0;
        }
    }

    // Initialize active processes counter
    shm->active_procs = 0;
}

void cleanup(int shmid, int msgid) {
    // Remove shared memory
    if (shmid != -1) {
        shmdt(NULL);
        shmctl(shmid, IPC_RMID, NULL);
    }

    // Remove message queue
    if (msgid != -1) {
        msgctl(msgid, IPC_RMID, NULL);
    }

    // Close log file
    if (g_logfile != NULL) {
        fclose(g_logfile);
    }
}

void signal_handler(int sig) {
    printf("\nReceived signal %d. Cleaning up and terminating...\n", sig);
    cleanup(g_shmid, g_msgid);
    exit(EXIT_SUCCESS);
}

int find_free_pcb(SharedMemory *shm) {
    for (int i = 0; i < MAX_PROC; i++) {
        if (shm->process_table[i].state == UNUSED) {
            return i;
        }
    }
    return -1;
}

void release_resources(SharedMemory *shm, int local_pid) {
    log_message(g_logfile, "Resources released by P%d: ", local_pid);

    for (int i = 0; i < MAX_RESOURCES; i++) {
        if (shm->resources.allocated[local_pid][i] > 0) {
            log_message(g_logfile, "R%d:%d ", i, shm->resources.allocated[local_pid][i]);

            // Release the resources
            shm->resources.available[i] += shm->resources.allocated[local_pid][i];
            shm->resources.allocated[local_pid][i] = 0;
        }
    }
    log_message(g_logfile, "\n");

    // Reset process state and resources
    shm->process_table[local_pid].state = UNUSED;
    shm->process_table[local_pid].pid = 0;

    // Check if any waiting processes can now be granted resources
    for (int i = 0; i < MAX_PROC; i++) {
        if (shm->process_table[i].state == BLOCKED) {
            int can_allocate = 1;

            for (int j = 0; j < MAX_RESOURCES; j++) {
                if (shm->resources.request[i][j] > shm->resources.available[j]) {
                    can_allocate = 0;
                    break;
                }
            }

            if (can_allocate) {
                // Allocate requested resources
                for (int j = 0; j < MAX_RESOURCES; j++) {
                    if (shm->resources.request[i][j] > 0) {
                        shm->resources.available[j] -= shm->resources.request[i][j];
                        shm->resources.allocated[i][j] += shm->resources.request[i][j];
                        shm->resources.request[i][j] = 0;
                    }
                }

                // Send grant message
                Message msg;
                msg.mtype = GRANT_MSG;
                msg.pid = shm->process_table[i].pid;
                msg.local_pid = i;

                if (msgsnd(g_msgid, &msg, sizeof(Message) - sizeof(long), 0) == -1) {
                    perror("msgsnd failed");
                }

                // Update process state
                shm->process_table[i].state = READY;

                log_message(g_logfile, "Master granting P%d previously blocked request at time %u:%u\n",
                           i, shm->clock.seconds, shm->clock.nanoseconds);

                requests_granted_after_wait++;
            }
        }
    }
}

void terminate_process(SharedMemory *shm, int local_pid, int is_deadlock) {
    log_message(g_logfile, "Process P%d terminated %s at time %u:%u\n",
               local_pid,
               is_deadlock ? "due to deadlock" : "normally",
               shm->clock.seconds, shm->clock.nanoseconds);

    // Update statistics
    if (is_deadlock) {
        processes_terminated_deadlock++;
    } else {
        processes_terminated_normally++;
    }

    // Kill the actual process
    kill(shm->process_table[local_pid].pid, SIGTERM);

    // Release resources and update process table
    release_resources(shm, local_pid);

    // Decrement active processes counter
    shm->active_procs--;
}

int detectDeadlock(ResourceDescriptor *rd, PCB *process_table, int active_procs, int *deadlocked) {
    int work[MAX_RESOURCES];
    int finish[MAX_PROC] = {0};
    int deadlock_count = 0;

    // Initialize work array with available resources
    for (int i = 0; i < MAX_RESOURCES; i++) {
        work[i] = rd->available[i];
    }

    // Find processes that can finish
    int found = 1;
    while (found) {
        found = 0;
        for (int i = 0; i < MAX_PROC; i++) {
            if (process_table[i].state != UNUSED && process_table[i].state != TERMINATED && !finish[i]) {
                // Check if process can get all needed resources
                int can_finish = 1;
                for (int j = 0; j < MAX_RESOURCES; j++) {
                    if (rd->request[i][j] > work[j]) {
                        can_finish = 0;
                        break;
                    }
                }

                if (can_finish) {
                    // Process can finish, add its resources to work
                    for (int j = 0; j < MAX_RESOURCES; j++) {
                        work[j] += rd->allocated[i][j];
                    }
                    finish[i] = 1;
                    found = 1;
                }
            }
        }
    }

    // Check for deadlocked processes
    for (int i = 0; i < MAX_PROC; i++) {
        if (process_table[i].state != UNUSED && process_table[i].state != TERMINATED && !finish[i]) {
            deadlocked[deadlock_count++] = i;
        }
    }

    return deadlock_count;
}

int main(int argc, char *argv[]) {
    int opt;
    int max_processes = 5;  // Default value
    int max_concurrent = 5; // Default value
    int interval_ms = 100;  // Default ms between child launches
    char logfilename[256] = "oss.log";
    int total_processes = 0;
    time_t start_time;
    int resource_check_counter = 0;
    int deadlock_check_counter = 0;
    int table_print_counter = 0;

    // Parse command line arguments
    while ((opt = getopt(argc, argv, "hn:s:i:f:v")) != -1) {
        switch (opt) {
            case 'h':
                printf("Usage: %s [-h] [-n proc] [-s simul] [-i interval] [-f logfile] [-v]\n", argv[0]);
                printf("  -h            : Show this help message\n");
                printf("  -n proc       : Maximum number of total processes to launch (default: 5)\n");
                printf("  -s simul      : Maximum number of concurrent processes (default: 5)\n");
                printf("  -i interval   : Interval in ms between child launches (default: 100)\n");
                printf("  -f logfile    : Log file name (default: oss.log)\n");
                printf("  -v            : Toggle verbose output off (default: on)\n");
                exit(EXIT_SUCCESS);
                break;
            case 'n':
                max_processes = atoi(optarg);
                break;
            case 's':
                max_concurrent = atoi(optarg);
                if (max_concurrent > MAX_PROC) {
                    fprintf(stderr, "Warning: Maximum concurrent processes is %d. Setting to %d.\n", MAX_PROC, MAX_PROC);
                    max_concurrent = MAX_PROC;
                }
                break;
            case 'i':
                interval_ms = atoi(optarg);
                break;
            case 'f':
                strncpy(logfilename, optarg, sizeof(logfilename) - 1);
                break;
            case 'v':
                g_verbose = 0;  // Turn off verbose output
                break;
            default:
                fprintf(stderr, "Invalid option. Use -h for help.\n");
                exit(EXIT_FAILURE);
        }
    }

    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Open log file
    g_logfile = fopen(logfilename, "w");
    if (g_logfile == NULL) {
        perror("Failed to open log file");
        exit(EXIT_FAILURE);
    }

    // Create shared memory segment
    g_shmid = shmget(SHM_KEY, sizeof(SharedMemory), IPC_CREAT | 0666);
    if (g_shmid == -1) {
        perror("shmget failed");
        cleanup(-1, -1);
        exit(EXIT_FAILURE);
    }

    // Attach to shared memory
    SharedMemory *shm = (SharedMemory *) shmat(g_shmid, NULL, 0);
    if (shm == (SharedMemory *) -1) {
        perror("shmat failed");
        cleanup(g_shmid, -1);
        exit(EXIT_FAILURE);
    }

    // Create message queue
    g_msgid = msgget(MSG_KEY, IPC_CREAT | 0666);
    if (g_msgid == -1) {
        perror("msgget failed");
        cleanup(g_shmid, -1);
        exit(EXIT_FAILURE);
    }

    // Initialize shared memory
    init_shared_memory(shm);

    log_message(g_logfile, "OSS: Resource Management System Started\n");

    // Record start time
    start_time = time(NULL);

    // Main loop
    int next_launch_time_ms = interval_ms;

    while ((total_processes < max_processes || shm->active_procs > 0) &&
           (time(NULL) - start_time < 5)) {  // 5 second real-time limit

        // Increment the clock (small random increment)
        unsigned int ns_increment = (rand() % 1000) + 100;
        addTime(&shm->clock, 0, ns_increment);

        // Check if it's time to launch a new process
        int current_time_ms = (shm->clock.seconds * 1000) + (shm->clock.nanoseconds / 1000000);

        if (total_processes < max_processes &&
            shm->active_procs < max_concurrent &&
            current_time_ms >= next_launch_time_ms) {

            // Find free PCB
            int pcb_idx = find_free_pcb(shm);

            if (pcb_idx != -1) {
                pid_t child_pid = fork();

                if (child_pid == -1) {
                    perror("fork failed");
                } else if (child_pid == 0) {
                    // Child process
                    char local_pid_str[10];
                    sprintf(local_pid_str, "%d", pcb_idx);

                    execl("./user_proc", "user_proc", local_pid_str, NULL);

                    // If execl fails
                    perror("execl failed");
                    exit(EXIT_FAILURE);
                } else {
                    // Parent process
                    shm->process_table[pcb_idx].pid = child_pid;
                    shm->process_table[pcb_idx].state = READY;
                    shm->process_table[pcb_idx].startTime[0] = shm->clock.seconds;
                    shm->process_table[pcb_idx].startTime[1] = shm->clock.nanoseconds;
                    shm->active_procs++;
                    total_processes++;

                    log_message(g_logfile, "OSS: Process P%d created at time %u:%u\n",
                               pcb_idx, shm->clock.seconds, shm->clock.nanoseconds);
                }
            }

            // Set next launch time
            next_launch_time_ms = current_time_ms + interval_ms;
        }

        // Check for terminated processes
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);

        if (pid > 0) {
            // Find the process in our table
            for (int i = 0; i < MAX_PROC; i++) {
                if (shm->process_table[i].pid == pid) {
                    terminate_process(shm, i, 0);  // Normal termination
                    break;
                }
            }
        }

        // Check for messages from child processes
        Message msg;
        if (msgrcv(g_msgid, &msg, sizeof(Message) - sizeof(long), 0, IPC_NOWAIT) != -1) {
            int local_pid = msg.local_pid;

            switch (msg.mtype) {
                case REQUEST_MSG: {
                    int resource_id = msg.resource_id;
                    int quantity = msg.quantity;

                    if (g_verbose) {
                        log_message(g_logfile, "Master has detected Process P%d requesting R%d at time %u:%u\n",
                                   local_pid, resource_id, shm->clock.seconds, shm->clock.nanoseconds);
                    }

                    // Check if request can be granted
                    if (quantity <= shm->resources.available[resource_id]) {
                        // Grant the request
                        shm->resources.available[resource_id] -= quantity;
                        shm->resources.allocated[local_pid][resource_id] += quantity;

                        // Send grant message
                        msg.mtype = GRANT_MSG;
                        if (msgsnd(g_msgid, &msg, sizeof(Message) - sizeof(long), 0) == -1) {
                            perror("msgsnd failed");
                        }

                        if (g_verbose) {
                            log_message(g_logfile, "Master granting P%d request R%d at time %u:%u\n",
                                       local_pid, resource_id, shm->clock.seconds, shm->clock.nanoseconds);
                        }

                        requests_granted_immediately++;
                    } else {
                        // Add process to wait queue
                        shm->process_table[local_pid].state = BLOCKED;
                        shm->resources.request[local_pid][resource_id] += quantity;

                        if (g_verbose) {
                            log_message(g_logfile, "Master: no instances of R%d available, P%d added to wait queue at time %u:%u\n",
                                       resource_id, local_pid, shm->clock.seconds, shm->clock.nanoseconds);
                        }
                    }
                    break;
                }

                case RELEASE_MSG: {
                    int resource_id = msg.resource_id;
                    int quantity = msg.quantity;

                    if (g_verbose) {
                        log_message(g_logfile, "Master has acknowledged Process P%d releasing R%d at time %u:%u\n",
                                   local_pid, resource_id, shm->clock.seconds, shm->clock.nanoseconds);
                        log_message(g_logfile, "Resources released: R%d:%d\n", resource_id, quantity);
                    }

                    // Return resources to available pool
                    shm->resources.available[resource_id] += quantity;
                    shm->resources.allocated[local_pid][resource_id] -= quantity;

                    // Check if any waiting processes can now be granted resources
                    for (int i = 0; i < MAX_PROC; i++) {
                        if (shm->process_table[i].state == BLOCKED &&
                            shm->resources.request[i][resource_id] > 0 &&
                            shm->resources.request[i][resource_id] <= shm->resources.available[resource_id]) {

                            // Grant the resources
                            shm->resources.available[resource_id] -= shm->resources.request[i][resource_id];
                            shm->resources.allocated[i][resource_id] += shm->resources.request[i][resource_id];

                            // Clear the request
                            int request_quantity = shm->resources.request[i][resource_id];
                            shm->resources.request[i][resource_id] = 0;

                            // Check if all requests are satisfied
                            int all_satisfied = 1;
                            for (int j = 0; j < MAX_RESOURCES; j++) {
                                if (shm->resources.request[i][j] > 0) {
                                    all_satisfied = 0;
                                    break;
                                }
                            }

                            if (all_satisfied) {
                                // Send grant message
                                Message grant_msg;
                                grant_msg.mtype = GRANT_MSG;
                                grant_msg.pid = shm->process_table[i].pid;
                                grant_msg.local_pid = i;

                                if (msgsnd(g_msgid, &grant_msg, sizeof(Message) - sizeof(long), 0) == -1) {
                                    perror("msgsnd failed");
                                }

                                // Update process state
                                shm->process_table[i].state = READY;

                                if (g_verbose) {
                                    log_message(g_logfile, "Master granting P%d previously blocked request R%d:%d at time %u:%u\n",
                                               i, resource_id, request_quantity, shm->clock.seconds, shm->clock.nanoseconds);
                                }

                                requests_granted_after_wait++;
                            }
                        }
                    }
                    break;
                }

                case TERMINATE_MSG: {
                    log_message(g_logfile, "Process P%d is terminating at time %u:%u\n",
                               local_pid, shm->clock.seconds, shm->clock.nanoseconds);

                    // Release all resources held by the process
                    release_resources(shm, local_pid);

                    // Decrement active processes counter
                    shm->active_procs--;
                    processes_terminated_normally++;
                    break;
                }
            }
        }

        // Check if 0.5 seconds have passed for resource table output
        table_print_counter += ns_increment;
        if (table_print_counter >= 500000000) {  // 0.5 second in ns
            print_resource_table(g_logfile, shm);
            table_print_counter = 0;
        }

        // Check if 1 second has passed for deadlock detection
        deadlock_check_counter += ns_increment;
        if (deadlock_check_counter >= 1000000000) {  // 1 second in ns
            int deadlocked[MAX_PROC];
            int deadlock_count = detectDeadlock(&shm->resources, shm->process_table, shm->active_procs, deadlocked);

            deadlock_runs++;

            if (deadlock_count > 0) {
                log_message(g_logfile, "Master running deadlock detection at time %u:%u:\n",
                           shm->clock.seconds, shm->clock.nanoseconds);
                log_message(g_logfile, "Processes ");

                for (int i = 0; i < deadlock_count; i++) {
                    log_message(g_logfile, "P%d%s", deadlocked[i], (i < deadlock_count - 1) ? ", " : "");
                }
                log_message(g_logfile, " deadlocked\n");

                processes_deadlocked_total += deadlock_count;

                // Resolve deadlock by terminating processes one by one
                for (int i = 0; i < deadlock_count; i++) {
                    int victim = deadlocked[i];

                    log_message(g_logfile, "Master terminating P%d to remove deadlock\n", victim);

                    // Try to resolve deadlock after each termination
                    terminate_process(shm, victim, 1);  // Deadlock termination

                    // Check if deadlock is resolved
                    int new_deadlocked[MAX_PROC];
                    int new_count = detectDeadlock(&shm->resources, shm->process_table, shm->active_procs, new_deadlocked);

                    if (new_count == 0) {
                        log_message(g_logfile, "Deadlock resolved after terminating %d processes\n", i + 1);
                        break;
                    }
                }
            } else {
                if (g_verbose) {
                    log_message(g_logfile, "Master running deadlock detection at time %u:%u: No deadlocks detected\n",
                               shm->clock.seconds, shm->clock.nanoseconds);
                }
            }

            deadlock_check_counter = 0;
        }
    }

    // Wait for any remaining children to terminate
    while (shm->active_procs > 0) {
        int status;
        pid_t pid = waitpid(-1, &status, 0);

        if (pid > 0) {
            for (int i = 0; i < MAX_PROC; i++) {
                if (shm->process_table[i].pid == pid) {
                    terminate_process(shm, i, 0);  // Normal termination
                    break;
                }
            }
        }
    }

    // Print statistics
    log_message(g_logfile, "\n--- Final Statistics ---\n");
    log_message(g_logfile, "Total processes: %d\n", total_processes);
    log_message(g_logfile, "Requests granted immediately: %d\n", requests_granted_immediately);
    log_message(g_logfile, "Requests granted after waiting: %d\n", requests_granted_after_wait);
    log_message(g_logfile, "Processes terminated normally: %d\n", processes_terminated_normally);
    log_message(g_logfile, "Processes terminated due to deadlock: %d\n", processes_terminated_deadlock);
    log_message(g_logfile, "Deadlock detection algorithm runs: %d\n", deadlock_runs);

    if (deadlock_runs > 0) {
        double avg_deadlocked = (double)processes_deadlocked_total / deadlock_runs;
        log_message(g_logfile, "Average processes in deadlock per detection: %.2f\n", avg_deadlocked);
if (processes_deadlocked_total > 0) {
            double termination_percent = (double)processes_terminated_deadlock / processes_deadlocked_total * 100;
            log_message(g_logfile, "Percentage of deadlocked processes terminated: %.2f%%\n", termination_percent);
        }
    }

    // Clean up
    cleanup(g_shmid, g_msgid);

    return EXIT_SUCCESS;
}

// Add time to the system clock
void addTime(SystemClock *clock, unsigned int sec, unsigned int ns) {
    clock->nanoseconds += ns;
    clock->seconds += sec;


    // Adjust if nanoseconds overflow
    if (clock->nanoseconds >= 1000000000) {
        clock->seconds += clock->nanoseconds / 1000000000;
        clock->nanoseconds %= 1000000000;
    }
}
