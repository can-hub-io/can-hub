#include "protocol/ifconfig_message.h"

#include <string.h>

#include "protocol/message_header.h"
#include "protocol/wire.h"

#define IFCONFIG_INTERFACE_NAME_OFFSET 0
#define IFCONFIG_OP_OFFSET 16
#define IFCONFIG_BITRATE_OFFSET 20
#define IFCONFIG_REPLY_INTERFACE_NAME_OFFSET 0
#define IFCONFIG_REPLY_STATUS_OFFSET 16
#define ADMIN_IFCONFIG_AGENT_NAME_OFFSET 0
#define ADMIN_IFCONFIG_INTERFACE_NAME_OFFSET 128
#define ADMIN_IFCONFIG_OP_OFFSET 144
#define ADMIN_IFCONFIG_BITRATE_OFFSET 148
#define ADMIN_IFCONFIG_REPLY_STATUS_OFFSET 0

static size_t encode(uint8_t type, uint16_t body_size, uint8_t *buffer, size_t buffer_size, uint8_t **body);
static void copyName(char *destination, const uint8_t *source, size_t size);

/* ---------- public ---------- */

size_t IfconfigMessage_Encode(const IfconfigMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;

    if (self->interface_name[REGISTER_INTERFACE_NAME_SIZE - 1] != '\0') {
        return 0;
    }
    if (encode(kMESSAGE_TYPE_IFCONFIG, IFCONFIG_BODY_SIZE, buffer, buffer_size, &body) == 0) {
        return 0;
    }

    memcpy(body + IFCONFIG_INTERFACE_NAME_OFFSET, self->interface_name, strlen(self->interface_name));
    body[IFCONFIG_OP_OFFSET] = self->op;
    Wire_WriteU32(body + IFCONFIG_BITRATE_OFFSET, self->bitrate);

    return MESSAGE_HEADER_SIZE + IFCONFIG_BODY_SIZE;
}

bool IfconfigMessage_Decode(IfconfigMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < IFCONFIG_BODY_SIZE) {
        return false;
    }

    copyName(self->interface_name, payload + IFCONFIG_INTERFACE_NAME_OFFSET, REGISTER_INTERFACE_NAME_SIZE);
    self->op = payload[IFCONFIG_OP_OFFSET];
    self->bitrate = Wire_ReadU32(payload + IFCONFIG_BITRATE_OFFSET);

    return true;
}

size_t IfconfigReplyMessage_Encode(const IfconfigReplyMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;

    if (self->interface_name[REGISTER_INTERFACE_NAME_SIZE - 1] != '\0') {
        return 0;
    }
    if (encode(kMESSAGE_TYPE_IFCONFIG_REPLY, IFCONFIG_REPLY_BODY_SIZE, buffer, buffer_size, &body) == 0) {
        return 0;
    }

    memcpy(body + IFCONFIG_REPLY_INTERFACE_NAME_OFFSET, self->interface_name, strlen(self->interface_name));
    body[IFCONFIG_REPLY_STATUS_OFFSET] = self->status;

    return MESSAGE_HEADER_SIZE + IFCONFIG_REPLY_BODY_SIZE;
}

bool IfconfigReplyMessage_Decode(IfconfigReplyMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < IFCONFIG_REPLY_BODY_SIZE) {
        return false;
    }

    copyName(self->interface_name, payload + IFCONFIG_REPLY_INTERFACE_NAME_OFFSET, REGISTER_INTERFACE_NAME_SIZE);
    self->status = payload[IFCONFIG_REPLY_STATUS_OFFSET];

    return true;
}

size_t AdminIfconfigMessage_Encode(const AdminIfconfigMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;

    if (self->agent_name[REGISTER_AGENT_NAME_SIZE - 1] != '\0') {
        return 0;
    }
    if (self->interface_name[REGISTER_INTERFACE_NAME_SIZE - 1] != '\0') {
        return 0;
    }
    if (encode(kMESSAGE_TYPE_ADMIN_IFCONFIG, ADMIN_IFCONFIG_BODY_SIZE, buffer, buffer_size, &body) == 0) {
        return 0;
    }

    memcpy(body + ADMIN_IFCONFIG_AGENT_NAME_OFFSET, self->agent_name, strlen(self->agent_name));
    memcpy(body + ADMIN_IFCONFIG_INTERFACE_NAME_OFFSET, self->interface_name, strlen(self->interface_name));
    body[ADMIN_IFCONFIG_OP_OFFSET] = self->op;
    Wire_WriteU32(body + ADMIN_IFCONFIG_BITRATE_OFFSET, self->bitrate);

    return MESSAGE_HEADER_SIZE + ADMIN_IFCONFIG_BODY_SIZE;
}

bool AdminIfconfigMessage_Decode(AdminIfconfigMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < ADMIN_IFCONFIG_BODY_SIZE) {
        return false;
    }

    copyName(self->agent_name, payload + ADMIN_IFCONFIG_AGENT_NAME_OFFSET, REGISTER_AGENT_NAME_SIZE);
    copyName(self->interface_name, payload + ADMIN_IFCONFIG_INTERFACE_NAME_OFFSET, REGISTER_INTERFACE_NAME_SIZE);
    self->op = payload[ADMIN_IFCONFIG_OP_OFFSET];
    self->bitrate = Wire_ReadU32(payload + ADMIN_IFCONFIG_BITRATE_OFFSET);

    return true;
}

size_t AdminIfconfigReplyMessage_Encode(const AdminIfconfigReplyMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;

    if (encode(kMESSAGE_TYPE_ADMIN_IFCONFIG_REPLY, ADMIN_IFCONFIG_REPLY_BODY_SIZE, buffer, buffer_size, &body) == 0) {
        return 0;
    }

    body[ADMIN_IFCONFIG_REPLY_STATUS_OFFSET] = self->status;

    return MESSAGE_HEADER_SIZE + ADMIN_IFCONFIG_REPLY_BODY_SIZE;
}

bool AdminIfconfigReplyMessage_Decode(AdminIfconfigReplyMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < ADMIN_IFCONFIG_REPLY_BODY_SIZE) {
        return false;
    }

    self->status = payload[ADMIN_IFCONFIG_REPLY_STATUS_OFFSET];

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

static void copyName(char *destination, const uint8_t *source, size_t size)
{
    memcpy(destination, source, size);
    destination[size - 1] = '\0';
}
