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
 * opener writes the channel id as a one-byte header before any frame, which
 * both identifies the stream and materializes it on the peer immediately (an
 * empty QUIC stream is invisible until data flows). The peer adopts the
 * incoming stream and reads that first byte to bind it to its channel; from
 * there the stream is bidirectional and lossless from byte zero.
 */

#define QUIC_RELIABLE_STREAMS_MAX 8

/*
 * The reliable data plane carries bulk traffic, not the small control messages
 * the inline 4 KiB ring was sized for. On a high-RTT link the in-flight window
 * (bounded by the TX ring) caps throughput to ring/RTT, so a 4 KiB ring throttles
 * the stream and overflows under a burst (frames dropped on QueueTx failure).
 * Each reliable stream therefore gets a large heap-allocated TX ring, adopted by
 * its QuicControlChannel at open and freed on reset.
 */
#define QUIC_RELIABLE_TX_BUFFER_SIZE (64 * 1024)
/* RX framer must hold a full stream flow-control window of received-but-not-yet-
 * relayed bytes: when the peer we relay to is full we stop draining and withhold
 * the sender's credit, so the sender can have up to its window buffered here
 * before it blocks. Sized >= INITIAL_MAX_STREAM_DATA so nothing is ever dropped. */
#define QUIC_RELIABLE_RX_BUFFER_SIZE (64 * 1024)

typedef struct {
    QuicControlChannel stream;
    uint8_t *tx_storage;
    uint8_t *rx_storage;
    uint8_t channel;
    bool in_use;
    bool has_channel;
} QuicReliableStream;

typedef struct {
    QuicReliableStream streams[QUIC_RELIABLE_STREAMS_MAX];
} QuicReliableStreamSet;

/* Returns false when the downstream cannot accept the frame right now; the
 * receiver then stops draining and withholds flow-control credit (backpressure)
 * instead of dropping, so a reliable frame is never lost. */
typedef bool (*QuicReliableFrameSink)(void *context, const uint8_t *frame, size_t size);

void QuicReliableStreams_Reset(QuicReliableStreamSet *self);
QuicReliableStream *QuicReliableStreams_FindByChannel(QuicReliableStreamSet *self, uint8_t channel);
QuicReliableStream *QuicReliableStreams_FindById(QuicReliableStreamSet *self, int64_t stream_id);
QuicReliableStream *QuicReliableStreams_Open(QuicReliableStreamSet *self, QuicConnection *connection, uint8_t channel);
QuicReliableStream *QuicReliableStreams_Adopt(QuicReliableStreamSet *self, int64_t stream_id);
void QuicReliableStreams_Receive(
    QuicReliableStream *reliable,
    QuicConnection *connection,
    const uint8_t *data,
    size_t size,
    QuicReliableFrameSink sink,
    void *context
);
void QuicReliableStreams_RetryDrain(
    QuicReliableStreamSet *self,
    QuicConnection *connection,
    QuicReliableFrameSink sink,
    void *context
);
bool QuicReliableStreams_Drain(QuicReliableStreamSet *self, QuicConnection *connection, const QuicEgressSink *sink);

#endif
