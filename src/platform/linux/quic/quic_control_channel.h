#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "platform/linux/shared/message_framer.h"

#define QUIC_CONTROL_TX_BUFFER_SIZE 4096
#define QUIC_CONTROL_NO_STREAM (-1)

/*
 * Byte channel over one QUIC stream: TX retention until the peer acknowledges
 * (ngtcp2 does not copy stream data, so retained bytes must keep a stable
 * address while in flight), RX reassembly of length-delimited protocol
 * messages. The TX side is a ring buffer: acked bytes are reclaimed by
 * advancing the head, never by moving the still-in-flight tail. Pure buffer
 * logic, no I/O. Shared by the control plane and the reliable data streams.
 */
typedef struct {
    int64_t stream_id;
    uint64_t tx_base_offset;
    uint8_t tx_inline[QUIC_CONTROL_TX_BUFFER_SIZE];
    uint8_t *tx_external;
    size_t tx_capacity;
    size_t tx_head;
    size_t tx_used;
    size_t tx_sent;
    MessageFramer framer;
} QuicControlChannel;

void QuicControlChannel_Reset(QuicControlChannel *self);
void QuicControlChannel_AdoptBuffer(QuicControlChannel *self, uint8_t *buffer, size_t capacity);
void QuicControlChannel_AdoptRxBuffer(QuicControlChannel *self, uint8_t *buffer, size_t capacity);
bool QuicControlChannel_CanQueue(const QuicControlChannel *self, size_t size);
size_t QuicControlChannel_RxPending(const QuicControlChannel *self);
bool QuicControlChannel_QueueTx(QuicControlChannel *self, const uint8_t *data, size_t size);
size_t QuicControlChannel_PendingTx(const QuicControlChannel *self, const uint8_t **data);
void QuicControlChannel_MarkSent(QuicControlChannel *self, size_t size);
void QuicControlChannel_MarkAcked(QuicControlChannel *self, uint64_t acked_end_offset);
size_t QuicControlChannel_QueueRx(QuicControlChannel *self, const uint8_t *data, size_t size);
size_t QuicControlChannel_NextMessage(const QuicControlChannel *self, const uint8_t **message);
void QuicControlChannel_ConsumeMessage(QuicControlChannel *self, size_t size);
