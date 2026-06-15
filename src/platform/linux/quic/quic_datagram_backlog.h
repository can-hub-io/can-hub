#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol/control_buffer.h"

/*
 * Bounded mitigation buffer for data-plane datagrams ngtcp2 could not take right
 * away (congestion window full): ngtcp2 does not buffer unaccepted datagrams, so
 * without this a short burst loses the frames the window briefly refuses. This
 * only absorbs that transient; a push that overflows the ring still fails and
 * the frame is dropped (the plane stays lossy). It is not reliability and not
 * bus-rate pacing -- proper rate-matching is the design issue, not this buffer.
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
