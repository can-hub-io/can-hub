#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "protocol/register_message.h"

/*
 * AIMD credit per interface: the agent's measured-sustainable bus rate, emitted
 * upstream so the hub can pace to min(advertised, credit) instead of trusting
 * the advertised nominal blindly. Starts at the advertised rate; each status
 * window it backs off multiplicatively when new CAN-tx drops appeared (the bus
 * could not keep up) and ramps additively back toward advertised when none did.
 * A too-high advertised rate (or a contended bus) self-corrects; the floor keeps
 * some flow. Freestanding, no clock: exactly one Update per status window.
 */
typedef struct {
    uint32_t credit[REGISTER_INTERFACES_MAX];
    uint64_t last_tx_dropped[REGISTER_INTERFACES_MAX];
    bool seen[REGISTER_INTERFACES_MAX];
} TxPacer;

void TxPacer_Reset(TxPacer *self);
uint32_t TxPacer_Update(TxPacer *self, uint8_t interface_index, uint32_t advertised_rate, uint64_t tx_dropped);
