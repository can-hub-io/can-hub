#include "protocol/hello_message.h"

#include <string.h>

#include "protocol/message_header.h"
#include "protocol/wire.h"

#define VERSION_OFFSET 0
#define ROLE_OFFSET 1
#define CAPABILITIES_OFFSET 4
#define NAME_OFFSET 8

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
    memcpy(body + NAME_OFFSET, self->name, strnlen(self->name, HELLO_NAME_SIZE - 1));

    return total_size;
}

size_t HelloMessage_Build(uint8_t role, const char *name, uint32_t capabilities, uint8_t *buffer, size_t buffer_size)
{
    HelloMessage hello;
    size_t length;

    hello.version = PROTOCOL_VERSION;
    hello.role = role;
    hello.capabilities = capabilities;
    hello.name[0] = '\0';
    if (name != NULL) {
        length = strnlen(name, HELLO_NAME_SIZE - 1);
        memcpy(hello.name, name, length);
        hello.name[length] = '\0';
    }

    return HelloMessage_Encode(&hello, buffer, buffer_size);
}

bool HelloMessage_Decode(HelloMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < HELLO_BODY_SIZE) {
        return false;
    }

    self->version = payload[VERSION_OFFSET];
    self->role = payload[ROLE_OFFSET];
    self->capabilities = Wire_ReadU32(payload + CAPABILITIES_OFFSET);
    memcpy(self->name, payload + NAME_OFFSET, HELLO_NAME_SIZE);
    self->name[HELLO_NAME_SIZE - 1] = '\0';

    return isRoleValid(self->role);
}

/* ---------- private ---------- */

static bool isRoleValid(uint8_t role)
{
    return role >= kPEER_ROLE_AGENT && role < kPEER_ROLE_MAX;
}
