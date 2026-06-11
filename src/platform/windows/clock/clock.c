#include "platform/linux/clock/clock.h"

#include <windows.h>

#define NANOSECONDS_PER_SECOND 1000000000ULL
#define FILETIME_UNITS_PER_MICROSECOND 10ULL
#define FILETIME_EPOCH_OFFSET 116444736000000000ULL

uint64_t Clock_MonotonicNs(void)
{
    static LARGE_INTEGER frequency;
    LARGE_INTEGER counter;

    if (frequency.QuadPart == 0) {
        QueryPerformanceFrequency(&frequency);
    }
    QueryPerformanceCounter(&counter);

    return (uint64_t)((double)counter.QuadPart * NANOSECONDS_PER_SECOND / (double)frequency.QuadPart);
}

uint64_t Clock_RealtimeUs(void)
{
    FILETIME file_time;
    ULARGE_INTEGER stamp;

    GetSystemTimePreciseAsFileTime(&file_time);
    stamp.LowPart = file_time.dwLowDateTime;
    stamp.HighPart = file_time.dwHighDateTime;

    return (stamp.QuadPart - FILETIME_EPOCH_OFFSET) / FILETIME_UNITS_PER_MICROSECOND;
}
