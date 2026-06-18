#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Hub-facing transport for the agent core. The core only produces and
 * consumes wire-encoded protocol messages; how they travel (QUIC streams +
 * datagrams, TCP, UDP, lwIP on a microcontroller) is the adapter's business.
 *
 * Inbound events (connected, disconnected, control bytes, frame bytes) are
 * pushed into the core by the platform loop via the Agent_On* functions —
 * they do not travel through this port.
 *
 * send_control: reliable, ordered delivery required.
 * send_frame: best-effort latest-wins by default; reliable per channel once
 *   set_channel_mode marks it (the adapter routes that channel over a
 *   dedicated stream). channel identifies the data-plane flow.
 * set_channel_mode: declare whether a channel is reliable; the adapter owns
 *   how (dedicated stream vs datagram).
 */
typedef struct {
    void *context;
    bool (*connect)(void *context);
    void (*disconnect)(void *context);
    bool (*send_control)(void *context, const uint8_t *data, size_t size);
    bool (*send_frame)(void *context, uint8_t channel, const uint8_t *data, size_t size);
    void (*set_channel_mode)(void *context, uint8_t channel, bool reliable);
} TransportPort;
