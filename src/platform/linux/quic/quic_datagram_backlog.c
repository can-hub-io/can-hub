#include "platform/linux/quic/quic_datagram_backlog.h"

#include <string.h>

void QuicDatagramBacklog_Init(QuicDatagramBacklog *self, QuicDatagramSlot *slots, uint16_t capacity)
{
    self->slots = slots;
    self->capacity = capacity;
    QuicDatagramBacklog_Reset(self);
}

void QuicDatagramBacklog_Reset(QuicDatagramBacklog *self)
{
    self->head = 0;
    self->count = 0;
}

bool QuicDatagramBacklog_IsEmpty(const QuicDatagramBacklog *self)
{
    return self->count == 0;
}

uint16_t QuicDatagramBacklog_Count(const QuicDatagramBacklog *self)
{
    return self->count;
}

bool QuicDatagramBacklog_Push(QuicDatagramBacklog *self, const uint8_t *data, size_t size)
{
    uint16_t tail;

    if (size > FRAME_WIRE_SIZE || self->count == self->capacity) {
        return false;
    }

    tail = (self->head + self->count) % self->capacity;
    memcpy(self->slots[tail].data, data, size);
    self->slots[tail].size = (uint16_t)size;
    self->count++;

    return true;
}

const uint8_t *QuicDatagramBacklog_Front(const QuicDatagramBacklog *self, size_t *size)
{
    if (self->count == 0) {
        *size = 0;
        return NULL;
    }

    *size = self->slots[self->head].size;

    return self->slots[self->head].data;
}

void QuicDatagramBacklog_PopFront(QuicDatagramBacklog *self)
{
    if (self->count == 0) {
        return;
    }

    self->head = (self->head + 1) % self->capacity;
    self->count--;
}
