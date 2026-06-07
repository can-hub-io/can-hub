#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Inbound socketcand server contract, defined by the bridge core and
 * implemented by it (see SocketcandBridge_ServerEvents). The platform server
 * adapter invokes these when a local ASCII client connects, delivers bytes,
 * or disconnects. Counterpart of SocketcandServerPort.
 */
typedef struct {
    void *context;
    void (*on_client_connected)(void *context, uint32_t connection_id);
    void (*on_client_bytes)(void *context, uint32_t connection_id, const uint8_t *data, size_t size);
    void (*on_client_disconnected)(void *context, uint32_t connection_id);
} SocketcandServerEvents;
