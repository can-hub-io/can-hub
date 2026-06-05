#include "protocol/hello_message.h"

#include <string.h>

#include "protocol/message_header.h"
#include "protocol/wire.h"

#define VERSION_OFFSET 0
#define ROLE_OFFSET 1
#define CAPABILITIES_OFFSET 4

static bool isRoleValid(uint8_t role);

/* ---------- public ---------- */

size_t HelloMessage_Encode(const HelloMessage *self, uint8_t *buffer, size_t buffer_size)
{
    MessageHeader header;
    size_t total_size = MESSAGE_HEADER_SIZE + HELLO_BODY_SIZE;
    uint8_t *body;

    if (!isRoleValid(self->role)) {
        return 0;
    }
    if (buffer_size < total_size) {
        return 0;
    }

    header.type = kMESSAGE_TYPE_HELLO;
    header.flags = 0;
    header.length = HELLO_BODY_SIZE;
    MessageHeader_Encode(&header, buffer, buffer_size);

    body = buffer + MESSAGE_HEADER_SIZE;
    memset(body, 0, HELLO_BODY_SIZE);
    body[VERSION_OFFSET] = self->version;
    body[ROLE_OFFSET] = self->role;
    Wire_WriteU32(body + CAPABILITIES_OFFSET, self->capabilities);

    return total_size;
}

bool HelloMessage_Decode(HelloMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < HELLO_BODY_SIZE) {
        return false;
    }

    self->version = payload[VERSION_OFFSET];
    self->role = payload[ROLE_OFFSET];
    self->capabilities = Wire_ReadU32(payload + CAPABILITIES_OFFSET);

    return isRoleValid(self->role);
}

/* ---------- private ---------- */

static bool isRoleValid(uint8_t role)
{
    return role >= kPEER_ROLE_AGENT && role < kPEER_ROLE_MAX;
}
