#include "platform/linux/quic/quic_datagram_backlog.h"

#include <string.h>

void QuicDatagramBacklog_Reset(QuicDatagramBacklog *self)
{
    self->head = 0;
    self->count = 0;
}

bool QuicDatagramBacklog_IsEmpty(const QuicDatagramBacklog *self)
{
    return self->count == 0;
}

bool QuicDatagramBacklog_Push(QuicDatagramBacklog *self, const uint8_t *data, size_t size)
{
    size_t tail;

    if (size > FRAME_WIRE_SIZE || self->count == QUIC_DATAGRAM_BACKLOG_SLOTS) {
        return false;
    }

    tail = (self->head + self->count) % QUIC_DATAGRAM_BACKLOG_SLOTS;
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

    self->head = (self->head + 1) % QUIC_DATAGRAM_BACKLOG_SLOTS;
    self->count--;
}
