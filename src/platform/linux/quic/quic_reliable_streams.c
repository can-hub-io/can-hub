#include "platform/linux/quic/quic_reliable_streams.h"

#include <stdlib.h>

static bool findFreeSlot(const QuicReliableStreamSet *self, uint8_t *slot);
static bool findSlotById(const QuicReliableStreamSet *self, int64_t stream_id, uint8_t *slot);
static QuicReliableStream *claimSlot(QuicReliableStreamSet *self, uint8_t slot);
static void releaseSlot(QuicReliableStreamSet *self, uint8_t slot);
static void beginClosing(QuicReliableStream *reliable);
static bool attachStorage(QuicReliableStream *reliable);
static void drainAndCredit(QuicReliableStream *reliable, QuicConnection *connection, QuicReliableFrameSink sink, void *context);
static bool isClosing(const QuicReliableStreamSet *self, uint8_t channel);
static bool findReopen(const QuicReliableStreamSet *self, uint8_t channel, uint8_t *index);
static void forgetReopen(QuicReliableStreamSet *self, uint8_t channel);

/* ---------- public ---------- */

void QuicReliableStreams_Reset(QuicReliableStreamSet *self)
{
    uint8_t i;

    for(i=0; i<QUIC_RELIABLE_STREAMS_MAX; i++) {
        releaseSlot(self, i);
    }
    self->reopen_count = 0;
}

QuicReliableStream *QuicReliableStreams_FindByChannel(QuicReliableStreamSet *self, uint8_t channel)
{
    QuicReliableStream *reliable;
    uint8_t i;

    for(i=0; i<QUIC_RELIABLE_STREAMS_MAX; i++) {
        reliable = self->streams[i];
        if (reliable != NULL && !reliable->closing && reliable->has_channel && reliable->channel == channel) {
            return reliable;
        }
    }

    return NULL;
}

QuicReliableStream *QuicReliableStreams_FindById(QuicReliableStreamSet *self, int64_t stream_id)
{
    uint8_t slot;

    if (!findSlotById(self, stream_id, &slot)) {
        return NULL;
    }

    return self->streams[slot];
}

QuicReliableStream *QuicReliableStreams_Open(QuicReliableStreamSet *self, QuicConnection *connection, uint8_t channel)
{
    QuicReliableStream *reliable;
    int64_t stream_id;
    uint8_t slot;

    reliable = QuicReliableStreams_FindByChannel(self, channel);
    if (reliable != NULL) {
        return reliable;
    }
    if (!findFreeSlot(self, &slot)) {
        return NULL;
    }

    reliable = claimSlot(self, slot);
    if (reliable == NULL) {
        return NULL;
    }
    if (!QuicConnection_OpenControlStream(connection, &stream_id)) {
        releaseSlot(self, slot);
        return NULL;
    }
    reliable->stream.stream_id = stream_id;
    reliable->channel = channel;
    reliable->has_channel = true;
    QuicControlChannel_QueueTx(&reliable->stream, &channel, 1);

    return reliable;
}

QuicReliableStream *QuicReliableStreams_Adopt(QuicReliableStreamSet *self, int64_t stream_id)
{
    QuicReliableStream *reliable;
    uint8_t slot;

    if (!findFreeSlot(self, &slot)) {
        return NULL;
    }

    reliable = claimSlot(self, slot);
    if (reliable == NULL) {
        return NULL;
    }
    reliable->stream.stream_id = stream_id;

    return reliable;
}

QuicReliableStream *QuicReliableStreams_Close(QuicReliableStreamSet *self, uint8_t channel)
{
    QuicReliableStream *reliable = QuicReliableStreams_FindByChannel(self, channel);

    forgetReopen(self, channel);
    if (reliable == NULL) {
        return NULL;
    }

    beginClosing(reliable);

    return reliable;
}

bool QuicReliableStreams_DeferReopen(QuicReliableStreamSet *self, uint8_t channel)
{
    if (!isClosing(self, channel)) {
        return false;
    }
    if (!QuicReliableStreams_AwaitsReopen(self, channel) && self->reopen_count < QUIC_RELIABLE_STREAMS_MAX) {
        self->reopen_channels[self->reopen_count++] = channel;
    }

    return true;
}

bool QuicReliableStreams_AwaitsReopen(const QuicReliableStreamSet *self, uint8_t channel)
{
    uint8_t index;

    return findReopen(self, channel, &index);
}

bool QuicReliableStreams_TakeReopenable(QuicReliableStreamSet *self, uint8_t *channel)
{
    uint8_t i;

    for(i=0; i<self->reopen_count; i++) {
        if (!isClosing(self, self->reopen_channels[i])) {
            *channel = self->reopen_channels[i];
            forgetReopen(self, *channel);
            return true;
        }
    }

    return false;
}

void QuicReliableStreams_Finish(QuicReliableStreamSet *self, int64_t stream_id)
{
    QuicReliableStream *reliable = QuicReliableStreams_FindById(self, stream_id);

    if (reliable == NULL || reliable->closing) {
        return;
    }

    beginClosing(reliable);
}

void QuicReliableStreams_Release(QuicReliableStreamSet *self, int64_t stream_id)
{
    uint8_t slot;

    if (!findSlotById(self, stream_id, &slot)) {
        return;
    }
    if (QuicControlChannel_RxPending(&self->streams[slot]->stream) > 0) {
        self->streams[slot]->closed = true;
        return;
    }

    releaseSlot(self, slot);
}

void QuicReliableStreams_Receive(
    QuicReliableStream *reliable,
    QuicConnection *connection,
    const uint8_t *data,
    size_t size,
    QuicReliableFrameSink sink,
    void *context
)
{
    size_t offset = 0;
    size_t taken;

    if (!reliable->has_channel) {
        if (size == 0) {
            return;
        }
        reliable->channel = data[0];
        reliable->has_channel = true;
        offset = 1;
        QuicConnection_ExtendStreamCredit(connection, reliable->stream.stream_id, 1);
    }

    while (offset < size) {
        taken = QuicControlChannel_QueueRx(&reliable->stream, data + offset, size - offset);
        offset += taken;
        drainAndCredit(reliable, connection, sink, context);
        if (taken == 0) {
            break;
        }
    }
}

void QuicReliableStreams_RetryDrain(
    QuicReliableStreamSet *self,
    QuicConnection *connection,
    QuicReliableFrameSink sink,
    void *context
)
{
    QuicReliableStream *reliable;
    uint8_t i;

    for(i=0; i<QUIC_RELIABLE_STREAMS_MAX; i++) {
        reliable = self->streams[i];
        if (reliable == NULL) {
            continue;
        }
        if (QuicControlChannel_RxPending(&reliable->stream) > 0) {
            drainAndCredit(reliable, connection, sink, context);
        }
        if (reliable->closed && QuicControlChannel_RxPending(&reliable->stream) == 0) {
            releaseSlot(self, i);
        }
    }
}

bool QuicReliableStreams_Drain(QuicReliableStreamSet *self, QuicConnection *connection, const QuicEgressSink *sink)
{
    uint8_t i;

    for(i=0; i<QUIC_RELIABLE_STREAMS_MAX; i++) {
        if (self->streams[i] == NULL) {
            continue;
        }
        if (!QuicEgress_Drain(connection, &self->streams[i]->stream, sink)) {
            return false;
        }
    }

    return true;
}

/* ---------- private ---------- */

static bool findFreeSlot(const QuicReliableStreamSet *self, uint8_t *slot)
{
    uint8_t i;

    for(i=0; i<QUIC_RELIABLE_STREAMS_MAX; i++) {
        if (self->streams[i] == NULL) {
            *slot = i;
            return true;
        }
    }

    return false;
}

static bool findSlotById(const QuicReliableStreamSet *self, int64_t stream_id, uint8_t *slot)
{
    uint8_t i;

    for(i=0; i<QUIC_RELIABLE_STREAMS_MAX; i++) {
        if (self->streams[i] != NULL && self->streams[i]->stream.stream_id == stream_id) {
            *slot = i;
            return true;
        }
    }

    return false;
}

static QuicReliableStream *claimSlot(QuicReliableStreamSet *self, uint8_t slot)
{
    QuicReliableStream *reliable = calloc(1, sizeof(*reliable));

    if (reliable == NULL) {
        return NULL;
    }

    QuicControlChannel_Reset(&reliable->stream);
    if (!attachStorage(reliable)) {
        free(reliable);
        return NULL;
    }
    self->streams[slot] = reliable;

    return reliable;
}

static void releaseSlot(QuicReliableStreamSet *self, uint8_t slot)
{
    QuicReliableStream *reliable = self->streams[slot];

    if (reliable == NULL) {
        return;
    }

    free(reliable->tx_storage);
    free(reliable->rx_storage);
    free(reliable);
    self->streams[slot] = NULL;
}

static void beginClosing(QuicReliableStream *reliable)
{
    reliable->closing = true;
    QuicControlChannel_RequestFinish(&reliable->stream);
}

static bool attachStorage(QuicReliableStream *reliable)
{
    reliable->tx_storage = malloc(QUIC_RELIABLE_TX_BUFFER_SIZE);
    reliable->rx_storage = malloc(QUIC_RELIABLE_RX_BUFFER_SIZE);
    if (reliable->tx_storage == NULL || reliable->rx_storage == NULL) {
        free(reliable->tx_storage);
        free(reliable->rx_storage);
        return false;
    }

    QuicControlChannel_AdoptBuffer(&reliable->stream, reliable->tx_storage, QUIC_RELIABLE_TX_BUFFER_SIZE);
    QuicControlChannel_AdoptRxBuffer(&reliable->stream, reliable->rx_storage, QUIC_RELIABLE_RX_BUFFER_SIZE);

    return true;
}

static void drainAndCredit(QuicReliableStream *reliable, QuicConnection *connection, QuicReliableFrameSink sink, void *context)
{
    const uint8_t *message;
    size_t message_size;
    size_t credited = 0;

    for (;;) {
        message_size = QuicControlChannel_NextMessage(&reliable->stream, &message);
        if (message_size == 0) {
            break;
        }
        if (!sink(context, message, message_size)) {
            break;
        }
        QuicControlChannel_ConsumeMessage(&reliable->stream, message_size);
        credited += message_size;
    }

    if (credited > 0) {
        QuicConnection_ExtendStreamCredit(connection, reliable->stream.stream_id, credited);
    }
}

static bool isClosing(const QuicReliableStreamSet *self, uint8_t channel)
{
    const QuicReliableStream *reliable;
    uint8_t i;

    for(i=0; i<QUIC_RELIABLE_STREAMS_MAX; i++) {
        reliable = self->streams[i];
        if (reliable != NULL && reliable->closing && reliable->has_channel && reliable->channel == channel) {
            return true;
        }
    }

    return false;
}

static bool findReopen(const QuicReliableStreamSet *self, uint8_t channel, uint8_t *index)
{
    uint8_t i;

    for(i=0; i<self->reopen_count; i++) {
        if (self->reopen_channels[i] == channel) {
            *index = i;
            return true;
        }
    }

    return false;
}

static void forgetReopen(QuicReliableStreamSet *self, uint8_t channel)
{
    uint8_t index;

    if (!findReopen(self, channel, &index)) {
        return;
    }

    self->reopen_count--;
    self->reopen_channels[index] = self->reopen_channels[self->reopen_count];
}
