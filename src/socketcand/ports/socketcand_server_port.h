#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Local socketcand server: the bridge's outbound side toward ASCII clients.
 * connection_id identifies one accepted TCP connection (assigned by the
 * adapter). write_client and close_client act on a single connection;
 * send_beacon emits one discovery datagram to the broadcast address.
 */
typedef struct {
    void *context;
    bool (*write_client)(void *context, uint32_t connection_id, const uint8_t *data, size_t size);
    void (*close_client)(void *context, uint32_t connection_id);
    void (*send_beacon)(void *context, const uint8_t *data, size_t size);
} SocketcandServerPort;
