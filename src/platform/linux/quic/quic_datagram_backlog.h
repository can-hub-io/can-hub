#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol/control_buffer.h"

/*
 * Bounded FIFO of data-plane datagrams waiting for the egress pump: the ones
 * staged while one event is handled, so the pump packs them into as few QUIC
 * packets as it can, and the ones ngtcp2 could not take right away (congestion
 * window full), since ngtcp2 does not buffer unaccepted datagrams. A push that
 * overflows the ring fails and the frame is dropped (the plane stays lossy). It
 * is not reliability and not bus-rate pacing. The owner provides the slots, so
 * each transport sizes its own ring.
 */

typedef struct {
    uint8_t data[FRAME_WIRE_SIZE];
    uint16_t size;
} QuicDatagramSlot;

typedef struct {
    QuicDatagramSlot *slots;
    uint16_t capacity;
    uint16_t head;
    uint16_t count;
} QuicDatagramBacklog;

void QuicDatagramBacklog_Init(QuicDatagramBacklog *self, QuicDatagramSlot *slots, uint16_t capacity);
void QuicDatagramBacklog_Reset(QuicDatagramBacklog *self);
bool QuicDatagramBacklog_IsEmpty(const QuicDatagramBacklog *self);
uint16_t QuicDatagramBacklog_Count(const QuicDatagramBacklog *self);
bool QuicDatagramBacklog_Push(QuicDatagramBacklog *self, const uint8_t *data, size_t size);
const uint8_t *QuicDatagramBacklog_Front(const QuicDatagramBacklog *self, size_t *size);
void QuicDatagramBacklog_PopFront(QuicDatagramBacklog *self);
