#include "platform/linux/quic/quic_control_channel.h"

#include <string.h>

#include "protocol/message_header.h"

static uint8_t *txBuffer(const QuicControlChannel *self);

/* ---------- public ---------- */

void QuicControlChannel_Reset(QuicControlChannel *self)
{
    memset(self, 0, sizeof(*self));
    self->stream_id = QUIC_CONTROL_NO_STREAM;
    self->tx_external = NULL;
    self->tx_capacity = QUIC_CONTROL_TX_BUFFER_SIZE;
    MessageFramer_Reset(&self->framer);
}

void QuicControlChannel_AdoptBuffer(QuicControlChannel *self, uint8_t *buffer, size_t capacity)
{
    self->tx_external = buffer;
    self->tx_capacity = capacity;
    self->tx_head = 0;
    self->tx_used = 0;
    self->tx_sent = 0;
}

bool QuicControlChannel_QueueTx(QuicControlChannel *self, const uint8_t *data, size_t size)
{
    uint8_t *buffer = txBuffer(self);
    size_t write_index;
    size_t first_span;

    if (self->tx_used + size > self->tx_capacity) {
        return false;
    }

    write_index = (self->tx_head + self->tx_used) % self->tx_capacity;
    first_span = self->tx_capacity - write_index;
    if (first_span >= size) {
        memcpy(buffer + write_index, data, size);
    } else {
        memcpy(buffer + write_index, data, first_span);
        memcpy(buffer, data + first_span, size - first_span);
    }
    self->tx_used += size;

    return true;
}

size_t QuicControlChannel_PendingTx(const QuicControlChannel *self, const uint8_t **data)
{
    size_t start = (self->tx_head + self->tx_sent) % self->tx_capacity;
    size_t available = self->tx_used - self->tx_sent;
    size_t contiguous = self->tx_capacity - start;

    *data = txBuffer(self) + start;
    if (available < contiguous) {
        return available;
    }

    return contiguous;
}

void QuicControlChannel_MarkSent(QuicControlChannel *self, size_t size)
{
    self->tx_sent += size;
    if (self->tx_sent > self->tx_used) {
        self->tx_sent = self->tx_used;
    }
}

void QuicControlChannel_MarkAcked(QuicControlChannel *self, uint64_t acked_end_offset)
{
    size_t acked_bytes;

    if (acked_end_offset <= self->tx_base_offset) {
        return;
    }

    acked_bytes = (size_t)(acked_end_offset - self->tx_base_offset);
    if (acked_bytes > self->tx_used) {
        acked_bytes = self->tx_used;
    }

    self->tx_head = (self->tx_head + acked_bytes) % self->tx_capacity;
    self->tx_used -= acked_bytes;
    if (acked_bytes > self->tx_sent) {
        self->tx_sent = 0;
    } else {
        self->tx_sent -= acked_bytes;
    }
    self->tx_base_offset += acked_bytes;
}

size_t QuicControlChannel_QueueRx(QuicControlChannel *self, const uint8_t *data, size_t size)
{
    return MessageFramer_Push(&self->framer, data, size);
}

size_t QuicControlChannel_NextMessage(const QuicControlChannel *self, const uint8_t **message)
{
    return MessageFramer_NextMessage(&self->framer, message);
}

void QuicControlChannel_ConsumeMessage(QuicControlChannel *self, size_t size)
{
    MessageFramer_Consume(&self->framer, size);
}

/* ---------- private ---------- */

static uint8_t *txBuffer(const QuicControlChannel *self)
{
    if (self->tx_external != NULL) {
        return self->tx_external;
    }

    return (uint8_t *)self->tx_inline;
}
