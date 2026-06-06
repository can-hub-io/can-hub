#include "agent/domain/reconnect_backoff.h"

#define BACKOFF_FACTOR 2

/* ---------- public ---------- */

void ReconnectBackoff_Init(ReconnectBackoff *self, uint32_t initial_delay_ms, uint32_t max_delay_ms)
{
    self->initial_delay_ms = initial_delay_ms;
    self->max_delay_ms = max_delay_ms;
    self->current_delay_ms = initial_delay_ms;
}

void ReconnectBackoff_Reset(ReconnectBackoff *self)
{
    self->current_delay_ms = self->initial_delay_ms;
}

uint32_t ReconnectBackoff_NextDelayMs(ReconnectBackoff *self)
{
    uint32_t delay_ms = self->current_delay_ms;

    self->current_delay_ms *= BACKOFF_FACTOR;
    if (self->current_delay_ms > self->max_delay_ms) {
        self->current_delay_ms = self->max_delay_ms;
    }

    return delay_ms;
}
