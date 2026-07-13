#pragma once

#include <stdint.h>

/*
 * POSIX clock access shared by the Linux adapters.
 * MonotonicNs feeds ngtcp2 (which expects monotonic nanoseconds);
 * MonotonicUs drives domain timers and deadlines (immune to wall-clock steps);
 * RealtimeUs stamps captured CAN frames (microseconds since epoch).
 */
uint64_t Clock_MonotonicNs(void);
uint64_t Clock_MonotonicUs(void);
uint64_t Clock_RealtimeUs(void);
