#include "protocol/subscribe_message.h"

#include <string.h>

#include "protocol/message_header.h"
#include "protocol/wire.h"

#define SUBSCRIBE_CHANNEL_OFFSET 0
#define SUBSCRIBE_FILTER_COUNT_OFFSET 1
#define SUBSCRIBE_FILTERS_OFFSET 4
#define FILTER_CAN_ID_OFFSET 0
#define FILTER_CAN_MASK_OFFSET 4

/* ---------- public ---------- */

size_t SubscribeMessage_Encode(const SubscribeMessage *self, uint8_t *buffer, size_t buffer_size)
{
    MessageHeader header;
    size_t body_size = SUBSCRIBE_FIXED_FIELDS_SIZE + (size_t)self->filter_count * CAN_FILTER_SIZE;
    size_t total_size = MESSAGE_HEADER_SIZE + body_size;
    uint8_t *body;
    uint8_t *filter;
    uint8_t i;

    if (self->filter_count > SUBSCRIBE_FILTERS_MAX || buffer_size < total_size) {
        return 0;
    }

    header.type = kMESSAGE_TYPE_SUBSCRIBE;
    header.flags = 0;
    header.length = (uint16_t)body_size;
    MessageHeader_Encode(&header, buffer, buffer_size);

    body = buffer + MESSAGE_HEADER_SIZE;
    memset(body, 0, body_size);
    body[SUBSCRIBE_CHANNEL_OFFSET] = self->channel;
    body[SUBSCRIBE_FILTER_COUNT_OFFSET] = self->filter_count;

    for(i=0; i<self->filter_count; i++) {
        filter = body + SUBSCRIBE_FILTERS_OFFSET + (size_t)i * CAN_FILTER_SIZE;
        Wire_WriteU32(filter + FILTER_CAN_ID_OFFSET, self->filters[i].can_id);
        Wire_WriteU32(filter + FILTER_CAN_MASK_OFFSET, self->filters[i].can_mask);
    }

    return total_size;
}

bool SubscribeMessage_Decode(SubscribeMessage *self, const uint8_t *payload, size_t payload_length)
{
    const uint8_t *filter;
    uint8_t i;

    if (payload_length < SUBSCRIBE_FIXED_FIELDS_SIZE) {
        return false;
    }

    self->channel = payload[SUBSCRIBE_CHANNEL_OFFSET];
    self->filter_count = payload[SUBSCRIBE_FILTER_COUNT_OFFSET];

    if (self->filter_count > SUBSCRIBE_FILTERS_MAX) {
        return false;
    }
    if (payload_length < SUBSCRIBE_FIXED_FIELDS_SIZE + (size_t)self->filter_count * CAN_FILTER_SIZE) {
        return false;
    }

    for(i=0; i<self->filter_count; i++) {
        filter = payload + SUBSCRIBE_FILTERS_OFFSET + (size_t)i * CAN_FILTER_SIZE;
        self->filters[i].can_id = Wire_ReadU32(filter + FILTER_CAN_ID_OFFSET);
        self->filters[i].can_mask = Wire_ReadU32(filter + FILTER_CAN_MASK_OFFSET);
    }

    return true;
}
