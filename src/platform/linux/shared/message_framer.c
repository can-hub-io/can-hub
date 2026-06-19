#include "platform/linux/shared/message_framer.h"

#include <string.h>

#include "protocol/message_header.h"

/* ---------- public ---------- */

void MessageFramer_Reset(MessageFramer *self)
{
    self->buffer = self->inline_buffer;
    self->capacity = MESSAGE_FRAMER_BUFFER_SIZE;
    self->used = 0;
}

void MessageFramer_AdoptBuffer(MessageFramer *self, uint8_t *buffer, size_t capacity)
{
    self->buffer = buffer;
    self->capacity = capacity;
    self->used = 0;
}

size_t MessageFramer_Pending(const MessageFramer *self)
{
    return self->used;
}

size_t MessageFramer_Push(MessageFramer *self, const uint8_t *data, size_t size)
{
    size_t available = self->capacity - self->used;
    size_t taken = size < available ? size : available;

    memcpy(self->buffer + self->used, data, taken);
    self->used += taken;

    return taken;
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

void MessageFramer_Drain(MessageFramer *self, const MessageSink *sink)
{
    const uint8_t *message;
    size_t message_size;

    for (;;) {
        message_size = MessageFramer_NextMessage(self, &message);
        if (message_size == 0) {
            return;
        }

        sink->on_message(sink->context, message, message_size);
        MessageFramer_Consume(self, message_size);
    }
}
