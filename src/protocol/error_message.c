#include "protocol/error_message.h"

#include <string.h>

#include "protocol/message_header.h"
#include "protocol/wire.h"

#define CODE_OFFSET 0
#define DETAIL_OFFSET 4

/* ---------- public ---------- */

size_t ErrorMessage_Encode(const ErrorMessage *self, uint8_t *buffer, size_t buffer_size)
{
    MessageHeader header;
    size_t total_size = MESSAGE_HEADER_SIZE + ERROR_BODY_SIZE;
    uint8_t *body;

    if (self->detail[ERROR_DETAIL_SIZE - 1] != '\0') {
        return 0;
    }
    if (buffer_size < total_size) {
        return 0;
    }

    header.type = kMESSAGE_TYPE_ERROR;
    header.flags = 0;
    header.length = ERROR_BODY_SIZE;
    MessageHeader_Encode(&header, buffer, buffer_size);

    body = buffer + MESSAGE_HEADER_SIZE;
    memset(body, 0, ERROR_BODY_SIZE);
    Wire_WriteU16(body + CODE_OFFSET, self->code);
    memcpy(body + DETAIL_OFFSET, self->detail, strlen(self->detail));

    return total_size;
}

bool ErrorMessage_Decode(ErrorMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < ERROR_BODY_SIZE) {
        return false;
    }

    self->code = Wire_ReadU16(payload + CODE_OFFSET);
    memcpy(self->detail, payload + DETAIL_OFFSET, ERROR_DETAIL_SIZE);
    self->detail[ERROR_DETAIL_SIZE - 1] = '\0';

    return true;
}
