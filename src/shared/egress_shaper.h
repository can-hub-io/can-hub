#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Token bucket metered in bits, used to pace hub->agent data egress to the
 * advertised bus rate (the CAN bus is the fixed-rate sink). One shaper per
 * agent interface. Time is injected (now_us); the core stays freestanding.
 *
 * rate_bits_per_s == 0 means unpaced: TryConsume always succeeds and the plane
 * behaves exactly as before pacing existed. burst_bits caps how much credit can
 * accrue while idle, so a quiet interface cannot release an unbounded burst when
 * traffic resumes; it must be >= the largest single frame's wire cost.
 */
typedef struct {
    uint32_t rate_bits_per_s;
    uint64_t tokens_bits;
    uint64_t burst_bits;
    uint64_t remainder;
    uint64_t last_us;
} EgressShaper;

void EgressShaper_Init(EgressShaper *self, uint64_t now_us, uint32_t rate_bits_per_s, uint64_t burst_bits);
void EgressShaper_SetRate(EgressShaper *self, uint32_t rate_bits_per_s);
void EgressShaper_Refill(EgressShaper *self, uint64_t now_us);
bool EgressShaper_TryConsume(EgressShaper *self, uint64_t bits);
uint64_t EgressShaper_DelayUs(const EgressShaper *self, uint64_t bits);
