#include "platform/linux/quic/quic_reliable_streams.h"

#include <stdlib.h>

static QuicReliableStream *findFree(QuicReliableStreamSet *self);
static bool attachStorage(QuicReliableStream *reliable);

/* ---------- public ---------- */

void QuicReliableStreams_Reset(QuicReliableStreamSet *self)
{
    uint8_t i;

    for(i=0; i<QUIC_RELIABLE_STREAMS_MAX; i++) {
        free(self->streams[i].tx_storage);
        self->streams[i].tx_storage = NULL;
        self->streams[i].in_use = false;
    }
}

QuicReliableStream *QuicReliableStreams_FindByChannel(QuicReliableStreamSet *self, uint8_t channel)
{
    QuicReliableStream *reliable;
    uint8_t i;

    for(i=0; i<QUIC_RELIABLE_STREAMS_MAX; i++) {
        reliable = &self->streams[i];
        if (reliable->in_use && reliable->has_channel && reliable->channel == channel) {
            return reliable;
        }
    }

    return NULL;
}

QuicReliableStream *QuicReliableStreams_FindById(QuicReliableStreamSet *self, int64_t stream_id)
{
    QuicReliableStream *reliable;
    uint8_t i;

    for(i=0; i<QUIC_RELIABLE_STREAMS_MAX; i++) {
        reliable = &self->streams[i];
        if (reliable->in_use && reliable->stream.stream_id == stream_id) {
            return reliable;
        }
    }

    return NULL;
}

QuicReliableStream *QuicReliableStreams_Open(QuicReliableStreamSet *self, QuicConnection *connection, uint8_t channel)
{
    QuicReliableStream *reliable;
    int64_t stream_id;

    reliable = QuicReliableStreams_FindByChannel(self, channel);
    if (reliable != NULL) {
        return reliable;
    }
    if (!QuicConnection_OpenControlStream(connection, &stream_id)) {
        return NULL;
    }

    reliable = findFree(self);
    if (reliable == NULL) {
        return NULL;
    }

    QuicControlChannel_Reset(&reliable->stream);
    if (!attachStorage(reliable)) {
        return NULL;
    }
    reliable->stream.stream_id = stream_id;
    reliable->channel = channel;
    reliable->in_use = true;
    reliable->has_channel = true;
    QuicControlChannel_QueueTx(&reliable->stream, &channel, 1);

    return reliable;
}

QuicReliableStream *QuicReliableStreams_Adopt(QuicReliableStreamSet *self, int64_t stream_id)
{
    QuicReliableStream *reliable = findFree(self);

    if (reliable == NULL) {
        return NULL;
    }

    QuicControlChannel_Reset(&reliable->stream);
    if (!attachStorage(reliable)) {
        return NULL;
    }
    reliable->stream.stream_id = stream_id;
    reliable->channel = 0;
    reliable->in_use = true;
    reliable->has_channel = false;

    return reliable;
}

void QuicReliableStreams_Receive(
    QuicReliableStream *reliable,
    const uint8_t *data,
    size_t size,
    QuicReliableFrameSink sink,
    void *context
)
{
    const uint8_t *message;
    size_t message_size;
    size_t offset = 0;
    size_t taken;

    if (!reliable->has_channel) {
        if (size == 0) {
            return;
        }
        reliable->channel = data[0];
        reliable->has_channel = true;
        offset = 1;
    }

    while (offset < size) {
        taken = QuicControlChannel_QueueRx(&reliable->stream, data + offset, size - offset);
        offset += taken;
        for (;;) {
            message_size = QuicControlChannel_NextMessage(&reliable->stream, &message);
            if (message_size == 0) {
                break;
            }
            sink(context, message, message_size);
            QuicControlChannel_ConsumeMessage(&reliable->stream, message_size);
        }
        if (taken == 0) {
            break;
        }
    }
}

bool QuicReliableStreams_Drain(QuicReliableStreamSet *self, QuicConnection *connection, const QuicEgressSink *sink)
{
    uint8_t i;

    for(i=0; i<QUIC_RELIABLE_STREAMS_MAX; i++) {
        if (!self->streams[i].in_use) {
            continue;
        }
        if (!QuicEgress_Drain(connection, &self->streams[i].stream, sink)) {
            return false;
        }
    }

    return true;
}

/* ---------- private ---------- */

static QuicReliableStream *findFree(QuicReliableStreamSet *self)
{
    uint8_t i;

    for(i=0; i<QUIC_RELIABLE_STREAMS_MAX; i++) {
        if (!self->streams[i].in_use) {
            return &self->streams[i];
        }
    }

    return NULL;
}

static bool attachStorage(QuicReliableStream *reliable)
{
    free(reliable->tx_storage);
    reliable->tx_storage = malloc(QUIC_RELIABLE_TX_BUFFER_SIZE);
    if (reliable->tx_storage == NULL) {
        return false;
    }

    QuicControlChannel_AdoptBuffer(&reliable->stream, reliable->tx_storage, QUIC_RELIABLE_TX_BUFFER_SIZE);

    return true;
}
