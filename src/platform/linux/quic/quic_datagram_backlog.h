#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol/control_buffer.h"

/*
 * Bounded FIFO of data-plane datagrams the QUIC client transport could not hand
 * to ngtcp2 right away (congestion window full): ngtcp2 does not buffer
 * unaccepted datagrams, so without this the frame would be dropped. Pushes that
 * overflow the ring fail, turning silent loss into explicit backpressure the
 * caller propagates to its producer.
 */

#define QUIC_DATAGRAM_BACKLOG_SLOTS 1024

typedef struct {
    uint8_t data[FRAME_WIRE_SIZE];
    uint16_t size;
} QuicDatagramSlot;

typedef struct {
    QuicDatagramSlot slots[QUIC_DATAGRAM_BACKLOG_SLOTS];
    size_t head;
    size_t count;
} QuicDatagramBacklog;

void QuicDatagramBacklog_Reset(QuicDatagramBacklog *self);
bool QuicDatagramBacklog_IsEmpty(const QuicDatagramBacklog *self);
bool QuicDatagramBacklog_Push(QuicDatagramBacklog *self, const uint8_t *data, size_t size);
const uint8_t *QuicDatagramBacklog_Front(const QuicDatagramBacklog *self, size_t *size);
void QuicDatagramBacklog_PopFront(QuicDatagramBacklog *self);
