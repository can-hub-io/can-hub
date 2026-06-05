#include "platform/linux/quic/quic_control_channel.h"

#include <string.h>

#include "protocol/message_header.h"

/* ---------- public ---------- */

void QuicControlChannel_Reset(QuicControlChannel *self)
{
    memset(self, 0, sizeof(*self));
    self->stream_id = QUIC_CONTROL_NO_STREAM;
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
    if (self->rx_used + size > QUIC_CONTROL_RX_BUFFER_SIZE) {
        return false;
    }

    memcpy(self->rx_buffer + self->rx_used, data, size);
    self->rx_used += size;

    return true;
}

size_t QuicControlChannel_NextMessage(const QuicControlChannel *self, const uint8_t **message)
{
    MessageHeader header;
    size_t message_size;

    if (!MessageHeader_Decode(&header, self->rx_buffer, self->rx_used)) {
        return 0;
    }

    message_size = (size_t)MESSAGE_HEADER_SIZE + header.length;
    if (self->rx_used < message_size) {
        return 0;
    }

    *message = self->rx_buffer;

    return message_size;
}

void QuicControlChannel_ConsumeMessage(QuicControlChannel *self, size_t size)
{
    if (size > self->rx_used) {
        size = self->rx_used;
    }

    memmove(self->rx_buffer, self->rx_buffer + size, self->rx_used - size);
    self->rx_used -= size;
}
