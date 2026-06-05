#include "protocol/register_message.h"

#include <string.h>

#include "protocol/message_header.h"

#define AGENT_NAME_OFFSET 0
#define INTERFACE_COUNT_OFFSET 128
#define INTERFACE_NAMES_OFFSET 132

#define ACK_STATUS_OFFSET 0
#define ACK_INTERFACE_COUNT_OFFSET 1
#define ACK_CHANNELS_OFFSET 4

static bool areInterfaceNamesValid(const RegisterMessage *message);

/* ---------- public ---------- */

size_t RegisterMessage_Encode(const RegisterMessage *self, uint8_t *buffer, size_t buffer_size)
{
    MessageHeader header;
    size_t total_size = MESSAGE_HEADER_SIZE + REGISTER_BODY_SIZE;
    uint8_t *body;
    uint8_t i;

    if (self->agent_name[REGISTER_AGENT_NAME_SIZE - 1] != '\0') {
        return 0;
    }
    if (self->interface_count > REGISTER_INTERFACES_MAX || !areInterfaceNamesValid(self)) {
        return 0;
    }
    if (buffer_size < total_size) {
        return 0;
    }

    header.type = kMESSAGE_TYPE_REGISTER;
    header.flags = 0;
    header.length = REGISTER_BODY_SIZE;
    MessageHeader_Encode(&header, buffer, buffer_size);

    body = buffer + MESSAGE_HEADER_SIZE;
    memset(body, 0, REGISTER_BODY_SIZE);
    strncpy((char *)body + AGENT_NAME_OFFSET, self->agent_name, REGISTER_AGENT_NAME_SIZE - 1);
    body[INTERFACE_COUNT_OFFSET] = self->interface_count;
    for(i=0; i<self->interface_count; i++) {
        strncpy(
            (char *)body + INTERFACE_NAMES_OFFSET + i * REGISTER_INTERFACE_NAME_SIZE,
            self->interface_names[i],
            REGISTER_INTERFACE_NAME_SIZE - 1
        );
    }

    return total_size;
}

bool RegisterMessage_Decode(RegisterMessage *self, const uint8_t *payload, size_t payload_length)
{
    uint8_t i;

    if (payload_length < REGISTER_BODY_SIZE) {
        return false;
    }

    memcpy(self->agent_name, payload + AGENT_NAME_OFFSET, REGISTER_AGENT_NAME_SIZE);
    self->agent_name[REGISTER_AGENT_NAME_SIZE - 1] = '\0';
    self->interface_count = payload[INTERFACE_COUNT_OFFSET];
    if (self->interface_count > REGISTER_INTERFACES_MAX) {
        return false;
    }

    for(i=0; i<self->interface_count; i++) {
        memcpy(
            self->interface_names[i],
            payload + INTERFACE_NAMES_OFFSET + i * REGISTER_INTERFACE_NAME_SIZE,
            REGISTER_INTERFACE_NAME_SIZE
        );
        self->interface_names[i][REGISTER_INTERFACE_NAME_SIZE - 1] = '\0';
    }

    return areInterfaceNamesValid(self);
}

size_t RegisterAckMessage_Encode(const RegisterAckMessage *self, uint8_t *buffer, size_t buffer_size)
{
    MessageHeader header;
    size_t total_size = MESSAGE_HEADER_SIZE + REGISTER_ACK_BODY_SIZE;
    uint8_t *body;

    if (self->interface_count > REGISTER_INTERFACES_MAX) {
        return 0;
    }
    if (buffer_size < total_size) {
        return 0;
    }

    header.type = kMESSAGE_TYPE_REGISTER_ACK;
    header.flags = 0;
    header.length = REGISTER_ACK_BODY_SIZE;
    MessageHeader_Encode(&header, buffer, buffer_size);

    body = buffer + MESSAGE_HEADER_SIZE;
    memset(body, 0, REGISTER_ACK_BODY_SIZE);
    body[ACK_STATUS_OFFSET] = self->status;
    body[ACK_INTERFACE_COUNT_OFFSET] = self->interface_count;
    memcpy(body + ACK_CHANNELS_OFFSET, self->channels, self->interface_count);

    return total_size;
}

bool RegisterAckMessage_Decode(RegisterAckMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < REGISTER_ACK_BODY_SIZE) {
        return false;
    }

    self->status = payload[ACK_STATUS_OFFSET];
    self->interface_count = payload[ACK_INTERFACE_COUNT_OFFSET];
    if (self->interface_count > REGISTER_INTERFACES_MAX) {
        return false;
    }

    memcpy(self->channels, payload + ACK_CHANNELS_OFFSET, self->interface_count);

    return true;
}

/* ---------- private ---------- */

static bool areInterfaceNamesValid(const RegisterMessage *message)
{
    size_t name_length;
    uint8_t i;

    for(i=0; i<message->interface_count; i++) {
        if (message->interface_names[i][REGISTER_INTERFACE_NAME_SIZE - 1] != '\0') {
            return false;
        }
        name_length = strlen(message->interface_names[i]);
        if (name_length == 0) {
            return false;
        }
    }

    return true;
}
