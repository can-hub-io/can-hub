#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Outbound multi-peer transport for the hub core: send to or close one of
 * the N connected peers. peer_id is assigned by the platform transport and
 * is opaque to the core. Counterpart of HubTransportEvents.
 */
typedef struct {
    void *context;
    bool (*send_control)(void *context, uint32_t peer_id, const uint8_t *data, size_t size);
    bool (*send_frame)(void *context, uint32_t peer_id, const uint8_t *data, size_t size);
    void (*close_peer)(void *context, uint32_t peer_id);
} HubTransportPort;
