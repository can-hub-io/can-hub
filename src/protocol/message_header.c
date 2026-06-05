#include "protocol/message_header.h"

#include "protocol/wire.h"

/* ---------- public ---------- */

size_t MessageHeader_Encode(const MessageHeader *self, uint8_t *buffer, size_t buffer_size)
{
    if (buffer_size < MESSAGE_HEADER_SIZE) {
        return 0;
    }

    buffer[0] = self->type;
    buffer[1] = self->flags;
    Wire_WriteU16(buffer + 2, self->length);

    return MESSAGE_HEADER_SIZE;
}

bool MessageHeader_Decode(MessageHeader *self, const uint8_t *buffer, size_t buffer_size)
{
    if (buffer_size < MESSAGE_HEADER_SIZE) {
        return false;
    }

    self->type = buffer[0];
    self->flags = buffer[1];
    self->length = Wire_ReadU16(buffer + 2);

    return true;
}
