#ifndef PLATFORM_LINUX_QUIC_QUIC_RELIABLE_STREAMS_H
#define PLATFORM_LINUX_QUIC_QUIC_RELIABLE_STREAMS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "platform/linux/quic/quic_connection.h"
#include "platform/linux/quic/quic_control_channel.h"
#include "platform/linux/quic/quic_egress.h"

/*
 * Reliable data-plane channels over dedicated bidirectional QUIC streams,
 * shared by the client and server transports. Each channel that opts in to
 * reliable delivery (OPEN flag, gated by the HELLO capability) gets one
 * stream with its own TX-retention + RX-framer (a QuicControlChannel). The
 * local side opens one per channel for its own egress; a stream the peer
 * opened arrives as "incoming" and is adopted for receive only — its frames
 * carry their own channel, so no channel id has to be learned to relay them.
 */

#define QUIC_RELIABLE_STREAMS_MAX 8

typedef struct {
    QuicControlChannel stream;
    uint8_t channel;
    bool in_use;
    bool has_channel;
} QuicReliableStream;

typedef struct {
    QuicReliableStream streams[QUIC_RELIABLE_STREAMS_MAX];
} QuicReliableStreamSet;

typedef void (*QuicReliableFrameSink)(void *context, const uint8_t *frame, size_t size);

void QuicReliableStreams_Reset(QuicReliableStreamSet *self);
QuicReliableStream *QuicReliableStreams_FindByChannel(QuicReliableStreamSet *self, uint8_t channel);
QuicReliableStream *QuicReliableStreams_FindById(QuicReliableStreamSet *self, int64_t stream_id);
QuicReliableStream *QuicReliableStreams_Open(QuicReliableStreamSet *self, QuicConnection *connection, uint8_t channel);
QuicReliableStream *QuicReliableStreams_Adopt(QuicReliableStreamSet *self, int64_t stream_id);
void QuicReliableStreams_Receive(
    QuicReliableStream *reliable,
    const uint8_t *data,
    size_t size,
    QuicReliableFrameSink sink,
    void *context
);
bool QuicReliableStreams_Drain(QuicReliableStreamSet *self, QuicConnection *connection, const QuicEgressSink *sink);

#endif
