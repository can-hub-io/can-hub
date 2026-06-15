#include "shared/egress_shaper.h"

#define MICROSECONDS_PER_SECOND 1000000

void EgressShaper_Init(EgressShaper *self, uint64_t now_us, uint32_t rate_bits_per_s, uint64_t burst_bits)
{
    self->rate_bits_per_s = rate_bits_per_s;
    self->burst_bits = burst_bits;
    self->tokens_bits = burst_bits;
    self->remainder = 0;
    self->last_us = now_us;
}

void EgressShaper_SetRate(EgressShaper *self, uint32_t rate_bits_per_s)
{
    self->rate_bits_per_s = rate_bits_per_s;
    if (self->tokens_bits > self->burst_bits) {
        self->tokens_bits = self->burst_bits;
    }
}

void EgressShaper_Refill(EgressShaper *self, uint64_t now_us)
{
    uint64_t elapsed;
    uint64_t fill_us;
    uint64_t numerator;

    if (self->rate_bits_per_s == 0 || now_us <= self->last_us) {
        self->last_us = now_us;
        return;
    }

    elapsed = now_us - self->last_us;
    self->last_us = now_us;

    fill_us = (self->burst_bits * MICROSECONDS_PER_SECOND) / self->rate_bits_per_s + 1;
    if (elapsed >= fill_us) {
        self->tokens_bits = self->burst_bits;
        self->remainder = 0;
        return;
    }

    numerator = (uint64_t)self->rate_bits_per_s * elapsed + self->remainder;
    self->tokens_bits += numerator / MICROSECONDS_PER_SECOND;
    self->remainder = numerator % MICROSECONDS_PER_SECOND;

    if (self->tokens_bits > self->burst_bits) {
        self->tokens_bits = self->burst_bits;
        self->remainder = 0;
    }
}

bool EgressShaper_TryConsume(EgressShaper *self, uint64_t bits)
{
    if (self->rate_bits_per_s == 0) {
        return true;
    }
    if (bits > self->burst_bits) {
        bits = self->burst_bits;
    }
    if (self->tokens_bits < bits) {
        return false;
    }

    self->tokens_bits -= bits;

    return true;
}

uint64_t EgressShaper_DelayUs(const EgressShaper *self, uint64_t bits)
{
    uint64_t deficit;

    if (self->rate_bits_per_s == 0 || self->tokens_bits >= bits) {
        return 0;
    }

    deficit = bits - self->tokens_bits;

    return (deficit * MICROSECONDS_PER_SECOND + self->rate_bits_per_s - 1) / self->rate_bits_per_s;
}
