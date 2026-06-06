#include "protocol/open_message.h"

#include <string.h>

#include "protocol/message_header.h"
#include "protocol/wire.h"

#define OPEN_INTERFACE_ID_OFFSET 0
#define ACK_STATUS_OFFSET 0
#define ACK_CHANNEL_OFFSET 1
#define ACK_INTERFACE_ID_OFFSET 4
#define CLOSE_CHANNEL_OFFSET 0

/* ---------- public ---------- */

size_t OpenMessage_Encode(const OpenMessage *self, uint8_t *buffer, size_t buffer_size)
{
    MessageHeader header;
    size_t total_size = MESSAGE_HEADER_SIZE + OPEN_BODY_SIZE;
    uint8_t *body;

    if (buffer_size < total_size) {
        return 0;
    }

    header.type = kMESSAGE_TYPE_OPEN;
    header.flags = 0;
    header.length = OPEN_BODY_SIZE;
    MessageHeader_Encode(&header, buffer, buffer_size);

    body = buffer + MESSAGE_HEADER_SIZE;
    Wire_WriteU32(body + OPEN_INTERFACE_ID_OFFSET, self->interface_id);

    return total_size;
}

bool OpenMessage_Decode(OpenMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < OPEN_BODY_SIZE) {
        return false;
    }

    self->interface_id = Wire_ReadU32(payload + OPEN_INTERFACE_ID_OFFSET);

    return true;
}

size_t OpenAckMessage_Encode(const OpenAckMessage *self, uint8_t *buffer, size_t buffer_size)
{
    MessageHeader header;
    size_t total_size = MESSAGE_HEADER_SIZE + OPEN_ACK_BODY_SIZE;
    uint8_t *body;

    if (buffer_size < total_size) {
        return 0;
    }

    header.type = kMESSAGE_TYPE_OPEN_ACK;
    header.flags = 0;
    header.length = OPEN_ACK_BODY_SIZE;
    MessageHeader_Encode(&header, buffer, buffer_size);

    body = buffer + MESSAGE_HEADER_SIZE;
    memset(body, 0, OPEN_ACK_BODY_SIZE);
    body[ACK_STATUS_OFFSET] = self->status;
    body[ACK_CHANNEL_OFFSET] = self->channel;
    Wire_WriteU32(body + ACK_INTERFACE_ID_OFFSET, self->interface_id);

    return total_size;
}

bool OpenAckMessage_Decode(OpenAckMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < OPEN_ACK_BODY_SIZE) {
        return false;
    }

    self->status = payload[ACK_STATUS_OFFSET];
    self->channel = payload[ACK_CHANNEL_OFFSET];
    self->interface_id = Wire_ReadU32(payload + ACK_INTERFACE_ID_OFFSET);

    return true;
}

size_t CloseMessage_Encode(const CloseMessage *self, uint8_t *buffer, size_t buffer_size)
{
    MessageHeader header;
    size_t total_size = MESSAGE_HEADER_SIZE + CLOSE_BODY_SIZE;
    uint8_t *body;

    if (buffer_size < total_size) {
        return 0;
    }

    header.type = kMESSAGE_TYPE_CLOSE;
    header.flags = 0;
    header.length = CLOSE_BODY_SIZE;
    MessageHeader_Encode(&header, buffer, buffer_size);

    body = buffer + MESSAGE_HEADER_SIZE;
    memset(body, 0, CLOSE_BODY_SIZE);
    body[CLOSE_CHANNEL_OFFSET] = self->channel;

    return total_size;
}

bool CloseMessage_Decode(CloseMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < CLOSE_BODY_SIZE) {
        return false;
    }

    self->channel = payload[CLOSE_CHANNEL_OFFSET];

    return true;
}
