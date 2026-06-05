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
    if (self->tx_used + size > QUIC_CONTROL_TX_BUFFER_SIZE) {
        return false;
    }

    memcpy(self->tx_buffer + self->tx_used, data, size);
    self->tx_used += size;

    return true;
}

size_t QuicControlChannel_PendingTx(const QuicControlChannel *self, const uint8_t **data)
{
    *data = self->tx_buffer + self->tx_sent;

    return self->tx_used - self->tx_sent;
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

    memmove(self->tx_buffer, self->tx_buffer + acked_bytes, self->tx_used - acked_bytes);
    self->tx_used -= acked_bytes;
    self->tx_sent -= acked_bytes;
    self->tx_base_offset += acked_bytes;
}

bool QuicControlChannel_QueueRx(QuicControlChannel *self, const uint8_t *data, size_t size)
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
