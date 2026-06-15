#include "protocol/interface_status_message.h"

#include <string.h>

#include "protocol/message_header.h"
#include "protocol/wire.h"

#define INTERFACE_STATUS_COUNT_OFFSET 0
#define INTERFACE_STATUS_ENTRIES_OFFSET 4
#define INTERFACE_STATUS_ENTRY_CHANNEL_OFFSET 0
#define INTERFACE_STATUS_ENTRY_FLAGS_OFFSET 1
#define INTERFACE_STATUS_ENTRY_ADVERTISED_RATE_OFFSET 4
#define INTERFACE_STATUS_ENTRY_CREDIT_OFFSET 8
#define INTERFACE_STATUS_ENTRY_TX_DROPPED_OFFSET 12

static size_t encode(uint8_t type, uint16_t body_size, uint8_t *buffer, size_t buffer_size, uint8_t **body);

/* ---------- public ---------- */

size_t InterfaceStatusMessage_Encode(const InterfaceStatusMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;
    uint8_t *entry;
    uint8_t i;

    if (self->interface_count > REGISTER_INTERFACES_MAX) {
        return 0;
    }
    if (encode(kMESSAGE_TYPE_INTERFACE_STATUS, INTERFACE_STATUS_BODY_SIZE, buffer, buffer_size, &body) == 0) {
        return 0;
    }

    body[INTERFACE_STATUS_COUNT_OFFSET] = self->interface_count;
    for(i=0; i<self->interface_count; i++) {
        entry = body + INTERFACE_STATUS_ENTRIES_OFFSET + (size_t)i * INTERFACE_STATUS_ENTRY_SIZE;
        entry[INTERFACE_STATUS_ENTRY_CHANNEL_OFFSET] = self->entries[i].channel;
        entry[INTERFACE_STATUS_ENTRY_FLAGS_OFFSET] = self->entries[i].flags;
        Wire_WriteU32(entry + INTERFACE_STATUS_ENTRY_ADVERTISED_RATE_OFFSET, self->entries[i].advertised_rate);
        Wire_WriteU32(entry + INTERFACE_STATUS_ENTRY_CREDIT_OFFSET, self->entries[i].credit);
        Wire_WriteU64(entry + INTERFACE_STATUS_ENTRY_TX_DROPPED_OFFSET, self->entries[i].tx_dropped);
    }

    return MESSAGE_HEADER_SIZE + INTERFACE_STATUS_BODY_SIZE;
}

bool InterfaceStatusMessage_Decode(InterfaceStatusMessage *self, const uint8_t *payload, size_t payload_length)
{
    const uint8_t *entry;
    uint8_t i;

    if (payload_length < INTERFACE_STATUS_BODY_SIZE) {
        return false;
    }

    self->interface_count = payload[INTERFACE_STATUS_COUNT_OFFSET];
    if (self->interface_count > REGISTER_INTERFACES_MAX) {
        return false;
    }

    for(i=0; i<self->interface_count; i++) {
        entry = payload + INTERFACE_STATUS_ENTRIES_OFFSET + (size_t)i * INTERFACE_STATUS_ENTRY_SIZE;
        self->entries[i].channel = entry[INTERFACE_STATUS_ENTRY_CHANNEL_OFFSET];
        self->entries[i].flags = entry[INTERFACE_STATUS_ENTRY_FLAGS_OFFSET];
        self->entries[i].advertised_rate = Wire_ReadU32(entry + INTERFACE_STATUS_ENTRY_ADVERTISED_RATE_OFFSET);
        self->entries[i].credit = Wire_ReadU32(entry + INTERFACE_STATUS_ENTRY_CREDIT_OFFSET);
        self->entries[i].tx_dropped = Wire_ReadU64(entry + INTERFACE_STATUS_ENTRY_TX_DROPPED_OFFSET);
    }

    return true;
}

/* ---------- private ---------- */

static size_t encode(uint8_t type, uint16_t body_size, uint8_t *buffer, size_t buffer_size, uint8_t **body)
{
    MessageHeader header;
    size_t total_size = MESSAGE_HEADER_SIZE + body_size;

    if (buffer_size < total_size) {
        return 0;
    }

    header.type = type;
    header.flags = 0;
    header.length = body_size;
    MessageHeader_Encode(&header, buffer, buffer_size);

    *body = buffer + MESSAGE_HEADER_SIZE;
    memset(*body, 0, body_size);

    return total_size;
}
