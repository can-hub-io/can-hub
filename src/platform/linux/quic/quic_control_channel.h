#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "platform/linux/shared/message_framer.h"

#define QUIC_CONTROL_TX_BUFFER_SIZE 4096
#define QUIC_CONTROL_NO_STREAM (-1)

/*
 * Control-plane byte channel over one QUIC stream: TX retention until the
 * peer acknowledges (ngtcp2 does not copy stream data), RX reassembly of
 * length-delimited protocol messages. Pure buffer logic, no I/O.
 */
typedef struct {
    int64_t stream_id;
    uint64_t tx_base_offset;
    uint8_t tx_buffer[QUIC_CONTROL_TX_BUFFER_SIZE];
    size_t tx_used;
    size_t tx_sent;
    MessageFramer framer;
} QuicControlChannel;

void QuicControlChannel_Reset(QuicControlChannel *self);
bool QuicControlChannel_QueueTx(QuicControlChannel *self, const uint8_t *data, size_t size);
size_t QuicControlChannel_PendingTx(const QuicControlChannel *self, const uint8_t **data);
void QuicControlChannel_MarkSent(QuicControlChannel *self, size_t size);
void QuicControlChannel_MarkAcked(QuicControlChannel *self, uint64_t acked_end_offset);
bool QuicControlChannel_QueueRx(QuicControlChannel *self, const uint8_t *data, size_t size);
size_t QuicControlChannel_NextMessage(const QuicControlChannel *self, const uint8_t **message);
void QuicControlChannel_ConsumeMessage(QuicControlChannel *self, size_t size);
