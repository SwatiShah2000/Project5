# Project5
Project-5-4760

# Operating System Simulator - Resource Management

## Overview
This program simulates an operating system resource management system that implements deadlock detection and recovery. The simulation consists of two main components:
1. **OSS (Operating System Simulator)**: The master process that manages resources and detects deadlocks
2. **User Processes**: Child processes that request and release resources

## Implementation Details

### Resource Management Strategy
The implementation uses the **deadlock detection and recovery** strategy for resource management. The system periodically checks for deadlocks and resolves them by terminating processes when necessary.

### Key Components
1. **Shared Memory**: Used for clock and resource management
2. **Message Queue**: Used for inter-process communication
3. **Process Table**: Tracks active processes and their resource allocations
4. **Resource Descriptors**: Manages available resources and instances

### Deadlock Detection and Resolution
- The system uses a modified banker's algorithm to detect deadlocks.
- When a deadlock is detected, the system selects one process to terminate to break the deadlock.
- The selection policy for deadlock resolution is to terminate the first deadlocked process identified, which minimizes overall disruption to the system.

### Communication Protocol
The system uses message queues for communication between the master and user processes with the following message types:
- REQUEST_MSG: Process requesting a resource
- GRANT_MSG: Master granting a resource
- RELEASE_MSG: Process releasing a resource
- TERMINATE_MSG: Process signaling termination

## Compilation and Execution

### Compilation
To compile the program, use the provided makefile:
```
make clean
make
```

### Execution
To run the simulation:
```
./oss [-h] [-n proc] [-s simul] [-i intervalInMsToLaunchChildren] [-f logfile]
```

Where:
- `-h`: Display help message
- `-n proc`: Total number of processes to create (default: 100)
- `-s simul`: Maximum number of processes running simultaneously (default: 18)
- `-i interval`: Interval in milliseconds between process creation (default: 1000)
- `-f logfile`: Log file name (default: oss.log)

## Output
The program outputs resource allocation information to both the console and a log file. The output includes:
- Process creation events
- Resource request and allocation events
- Deadlock detection reports
- Process termination events
- Resource tables showing current allocations
- Final statistics when the program terminates

## Statistics
The program tracks several statistics:
- Requests granted immediately vs. after waiting
- Processes terminated normally vs. by deadlock resolution
- Number of deadlock detection runs
- Average number of processes in deadlocks
- Percentage of processes terminated due to deadlocks

## Termination
The program terminates when:
1. It has created the specified number of processes and all have terminated
2. The execution time exceeds 5 real-time seconds

## Limitations and Considerations
- The current implementation creates a fixed number of resources (5) with a fixed number of instances per resource (10).
- User processes make resource requests with an 80% probability vs. a 20% probability of releasing resources.
- Processes check if they should terminate every 250ms of simulated time.
- Processes must run for at least 1 second before considering termination.

## Files
- `oss.c`: Main Operating System Simulator implementation
- `user_proc.c`: User process implementation
- `resource.h`: Shared definitions and constants
- `utils.c` and `utils.h`: Utility functions for clock management
- `makefile`: Build configuration

## Author
Swati Sah 
