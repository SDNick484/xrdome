#define _POSIX_C_SOURCE 200809L /* clock_gettime */

#include "util/timing.h"

#include <time.h>

uint64_t xrd_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
