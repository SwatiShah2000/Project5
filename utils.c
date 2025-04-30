#include "utils.h"



// Compare two times
int compareTime(unsigned int sec1, unsigned int ns1, unsigned int sec2, unsigned int ns2) {
    if (sec1 < sec2) return -1;
    if (sec1 > sec2) return 1;
    if (ns1 < ns2) return -1;
    if (ns1 > ns2) return 1;
    return 0;
}

// Get the elapsed time from start to end
void getElapsedTime(unsigned int start_sec, unsigned int start_ns,
                   unsigned int end_sec, unsigned int end_ns,
                   unsigned int *elapsed_sec, unsigned int *elapsed_ns) {
    *elapsed_sec = end_sec - start_sec;
    if (end_ns < start_ns) {
        (*elapsed_sec)--;
        *elapsed_ns = 1000000000 + end_ns - start_ns;
    } else {
        *elapsed_ns = end_ns - start_ns;
    }
}
