#include "platform/linux/shared/message_framer.h"

#include <string.h>

#include "protocol/message_header.h"

/* ---------- public ---------- */

void MessageFramer_Reset(MessageFramer *self)
{
    self->used = 0;
}

bool MessageFramer_Push(MessageFramer *self, const uint8_t *data, size_t size)
{
    if (self->used + size > MESSAGE_FRAMER_BUFFER_SIZE) {
        return false;
    }

    memcpy(self->buffer + self->used, data, size);
    self->used += size;

    return true;
}

size_t MessageFramer_NextMessage(const MessageFramer *self, const uint8_t **message)
{
    MessageHeader header;
    size_t message_size;

    if (!MessageHeader_Decode(&header, self->buffer, self->used)) {
        return 0;
    }

    message_size = (size_t)MESSAGE_HEADER_SIZE + header.length;
    if (self->used < message_size) {
        return 0;
    }

    *message = self->buffer;

    return message_size;
}

void MessageFramer_Consume(MessageFramer *self, size_t size)
{
    if (size > self->used) {
        size = self->used;
    }

    memmove(self->buffer, self->buffer + size, self->used - size);
    self->used -= size;
}
