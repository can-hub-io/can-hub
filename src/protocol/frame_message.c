#include "protocol/frame_message.h"

#include <string.h>

#include "protocol/message_header.h"
#include "protocol/wire.h"

#define CAN_ID_OFFSET 0
#define TIMESTAMP_OFFSET 4
#define CHANNEL_OFFSET 12
#define PAYLOAD_LENGTH_OFFSET 13
#define FRAME_FLAGS_OFFSET 14
#define ROUTE_FLAGS_OFFSET 15
#define PAYLOAD_OFFSET 16

static bool isPayloadLengthValid(uint8_t payload_length, uint8_t frame_flags);

/* ---------- public ---------- */

size_t FrameMessage_Encode(const FrameMessage *self, uint8_t *buffer, size_t buffer_size)
{
    MessageHeader header;
    size_t total_size = MESSAGE_HEADER_SIZE + FRAME_FIXED_FIELDS_SIZE + self->payload_length;
    uint8_t *body;

    if (!isPayloadLengthValid(self->payload_length, self->frame_flags)) {
        return 0;
    }
    if (buffer_size < total_size) {
        return 0;
    }

    header.type = kMESSAGE_TYPE_FRAME;
    header.flags = 0;
    header.length = (uint16_t)(FRAME_FIXED_FIELDS_SIZE + self->payload_length);
    MessageHeader_Encode(&header, buffer, buffer_size);

    body = buffer + MESSAGE_HEADER_SIZE;
    Wire_WriteU32(body + CAN_ID_OFFSET, self->can_id);
    Wire_WriteU64(body + TIMESTAMP_OFFSET, self->timestamp_us);
    body[CHANNEL_OFFSET] = self->channel;
    body[PAYLOAD_LENGTH_OFFSET] = self->payload_length;
    body[FRAME_FLAGS_OFFSET] = self->frame_flags;
    body[ROUTE_FLAGS_OFFSET] = self->route_flags;
    memcpy(body + PAYLOAD_OFFSET, self->payload, self->payload_length);

    return total_size;
}

bool FrameMessage_Decode(FrameMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < FRAME_FIXED_FIELDS_SIZE) {
        return false;
    }

    self->can_id = Wire_ReadU32(payload + CAN_ID_OFFSET);
    self->timestamp_us = Wire_ReadU64(payload + TIMESTAMP_OFFSET);
    self->channel = payload[CHANNEL_OFFSET];
    self->payload_length = payload[PAYLOAD_LENGTH_OFFSET];
    self->frame_flags = payload[FRAME_FLAGS_OFFSET];
    self->route_flags = payload[ROUTE_FLAGS_OFFSET];

    if (!isPayloadLengthValid(self->payload_length, self->frame_flags)) {
        return false;
    }
    if (payload_length < (size_t)FRAME_FIXED_FIELDS_SIZE + self->payload_length) {
        return false;
    }

    memcpy(self->payload, payload + PAYLOAD_OFFSET, self->payload_length);

    return true;
}

/* ---------- private ---------- */

static bool isPayloadLengthValid(uint8_t payload_length, uint8_t frame_flags)
{
    if (frame_flags & FRAME_FLAG_FD) {
        return payload_length <= FRAME_PAYLOAD_MAX_FD;
    }
    return payload_length <= FRAME_PAYLOAD_MAX_CLASSIC;
}
