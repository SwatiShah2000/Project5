#ifndef UTILS_H
#define UTILS_H
#include "resource.h"


// Compare two times
// Returns: -1 if time1 < time2, 0 if equal, 1 if time1 > time2
int compareTime(unsigned int sec1, unsigned int ns1, unsigned int sec2, unsigned int ns2);

// Get the elapsed time from start to end
void getElapsedTime(unsigned int start_sec, unsigned int start_ns,
                    unsigned int end_sec, unsigned int end_ns,
                    unsigned int *elapsed_sec, unsigned int *elapsed_ns);

#endif
