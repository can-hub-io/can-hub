#pragma once

#include <stdint.h>

/*
 * POSIX clock access shared by the Linux adapters.
 * MonotonicNs feeds ngtcp2 (which expects monotonic nanoseconds);
 * RealtimeUs stamps captured CAN frames (microseconds since epoch).
 */
uint64_t Clock_MonotonicNs(void);
uint64_t Clock_RealtimeUs(void);
