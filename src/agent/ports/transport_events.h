#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Inbound transport contract, defined by the core and implemented by it
 * (see Agent_TransportEvents). The platform transport adapter invokes these
 * when the connection changes state or delivers data. Counterpart of
 * TransportPort, which carries the outbound direction.
 */
typedef struct {
    void *context;
    void (*on_connected)(void *context);
    void (*on_disconnected)(void *context, uint64_t now_us);
    void (*on_control)(void *context, const uint8_t *data, size_t size, uint64_t now_us);
    void (*on_frame)(void *context, const uint8_t *data, size_t size);
} TransportEvents;
