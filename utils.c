/*
 * Resource Management Simulation
 * Utility functions
 */

#include "resource.h"

// Add time to the system clock
void addTime(SystemClock *clock, unsigned int sec, unsigned int ns) {
    clock->nanoseconds += ns;
    clock->seconds += sec;

    // Handle nanosecond overflow
    while (clock->nanoseconds >= 1000000000) {
        clock->nanoseconds -= 1000000000;
        clock->seconds++;
    }
}
