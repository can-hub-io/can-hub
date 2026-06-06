#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Inbound multi-peer contract, defined by the core and implemented by it
 * (see Hub_Events). The platform transport invokes these as peers connect,
 * disconnect and deliver data. Counterpart of HubTransportPort.
 */
typedef struct {
    void *context;
    void (*on_peer_connected)(void *context, uint32_t peer_id);
    void (*on_peer_disconnected)(void *context, uint32_t peer_id, uint64_t now_us);
    void (*on_peer_control)(void *context, uint32_t peer_id, const uint8_t *data, size_t size, uint64_t now_us);
    void (*on_peer_frame)(void *context, uint32_t peer_id, const uint8_t *data, size_t size);
} HubTransportEvents;
