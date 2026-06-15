#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol/register_message.h"

/*
 * Periodic agent -> hub report of per-interface data-plane health. Carries the
 * CAN-tx drop counter (frames the sink refused on ENOBUFS, the silent
 * rate-impedance loss) so the hub can surface it in admin views. advertised_rate,
 * credit and flags are reserved (encoded zero) for the pacing and reliability work
 * that builds on this message; see doc/protocol.md.
 */

#define INTERFACE_STATUS_ENTRY_SIZE 20
#define INTERFACE_STATUS_BODY_SIZE (4 + REGISTER_INTERFACES_MAX * INTERFACE_STATUS_ENTRY_SIZE)

#define INTERFACE_STATUS_FLAG_RELIABLE 0x01

typedef struct {
    uint8_t channel;
    uint8_t flags;
    uint32_t advertised_rate;
    uint32_t credit;
    uint64_t tx_dropped;
} InterfaceStatusEntry;

typedef struct {
    uint8_t interface_count;
    InterfaceStatusEntry entries[REGISTER_INTERFACES_MAX];
} InterfaceStatusMessage;

size_t InterfaceStatusMessage_Encode(const InterfaceStatusMessage *self, uint8_t *buffer, size_t buffer_size);
bool InterfaceStatusMessage_Decode(InterfaceStatusMessage *self, const uint8_t *payload, size_t payload_length);
