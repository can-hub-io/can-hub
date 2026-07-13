#include "platform/linux/clock/clock.h"

#include <time.h>

#define NANOSECONDS_PER_SECOND 1000000000ULL
#define MICROSECONDS_PER_SECOND 1000000ULL
#define NANOSECONDS_PER_MICROSECOND 1000ULL

/* ---------- public ---------- */

uint64_t Clock_MonotonicNs(void)
{
    struct timespec time_point;

    clock_gettime(CLOCK_MONOTONIC, &time_point);

    return (
        (uint64_t)time_point.tv_sec * NANOSECONDS_PER_SECOND
        + (uint64_t)time_point.tv_nsec
    );
}

uint64_t Clock_MonotonicUs(void)
{
    return Clock_MonotonicNs() / NANOSECONDS_PER_MICROSECOND;
}

uint64_t Clock_RealtimeUs(void)
{
    struct timespec time_point;

    clock_gettime(CLOCK_REALTIME, &time_point);

    return (
        (uint64_t)time_point.tv_sec * MICROSECONDS_PER_SECOND
        + (uint64_t)time_point.tv_nsec / NANOSECONDS_PER_MICROSECOND
    );
}
