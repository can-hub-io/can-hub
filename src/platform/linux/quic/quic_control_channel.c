#include "platform/linux/quic/quic_control_channel.h"

#include <string.h>

#include "protocol/message_header.h"

/* ---------- public ---------- */

void QuicControlChannel_Reset(QuicControlChannel *self)
{
    memset(self, 0, sizeof(*self));
    self->stream_id = QUIC_CONTROL_NO_STREAM;
    MessageFramer_Reset(&self->framer);
}

bool QuicControlChannel_QueueTx(QuicControlChannel *self, const uint8_t *data, size_t size)
{
    size_t write_index;
    size_t first_span;

    if (self->tx_used + size > QUIC_CONTROL_TX_BUFFER_SIZE) {
        return false;
    }

    write_index = (self->tx_head + self->tx_used) % QUIC_CONTROL_TX_BUFFER_SIZE;
    first_span = QUIC_CONTROL_TX_BUFFER_SIZE - write_index;
    if (first_span >= size) {
        memcpy(self->tx_buffer + write_index, data, size);
    } else {
        memcpy(self->tx_buffer + write_index, data, first_span);
        memcpy(self->tx_buffer, data + first_span, size - first_span);
    }
    self->tx_used += size;

    return true;
}

size_t QuicControlChannel_PendingTx(const QuicControlChannel *self, const uint8_t **data)
{
    size_t start = (self->tx_head + self->tx_sent) % QUIC_CONTROL_TX_BUFFER_SIZE;
    size_t available = self->tx_used - self->tx_sent;
    size_t contiguous = QUIC_CONTROL_TX_BUFFER_SIZE - start;

    *data = self->tx_buffer + start;
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

    self->tx_head = (self->tx_head + acked_bytes) % QUIC_CONTROL_TX_BUFFER_SIZE;
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
