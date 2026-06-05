#pragma once

#include <stdint.h>

#define RECONNECT_DEFAULT_INITIAL_DELAY_MS 1000
#define RECONNECT_DEFAULT_MAX_DELAY_MS 60000

typedef struct {
    uint32_t initial_delay_ms;
    uint32_t max_delay_ms;
    uint32_t current_delay_ms;
} ReconnectBackoff;

void ReconnectBackoff_Init(ReconnectBackoff *self, uint32_t initial_delay_ms, uint32_t max_delay_ms);
void ReconnectBackoff_Reset(ReconnectBackoff *self);
uint32_t ReconnectBackoff_NextDelayMs(ReconnectBackoff *self);
