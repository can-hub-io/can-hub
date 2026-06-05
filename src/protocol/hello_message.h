#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HELLO_BODY_SIZE 8
#define PROTOCOL_VERSION 0

typedef enum tpeer_role_e {
    kPEER_ROLE_AGENT = 1,
    kPEER_ROLE_CLIENT = 2,
    kPEER_ROLE_ADMIN = 3,
    kPEER_ROLE_MAX,
} TPEER_ROLE;

typedef struct {
    uint8_t version;
    uint8_t role;
    uint32_t capabilities;
} HelloMessage;

size_t HelloMessage_Encode(const HelloMessage *self, uint8_t *buffer, size_t buffer_size);
bool HelloMessage_Decode(HelloMessage *self, const uint8_t *payload, size_t payload_length);
