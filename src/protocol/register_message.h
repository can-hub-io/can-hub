#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define REGISTER_BODY_SIZE 388
#define REGISTER_ACK_BODY_SIZE 20
#define REGISTER_AGENT_NAME_SIZE 128
#define REGISTER_INTERFACES_MAX 16
#define REGISTER_INTERFACE_NAME_SIZE 16

#define REGISTER_STATUS_OK 0

typedef struct {
    char agent_name[REGISTER_AGENT_NAME_SIZE];
    uint8_t interface_count;
    char interface_names[REGISTER_INTERFACES_MAX][REGISTER_INTERFACE_NAME_SIZE];
} RegisterMessage;

typedef struct {
    uint8_t status;
    uint8_t interface_count;
    uint8_t channels[REGISTER_INTERFACES_MAX];
} RegisterAckMessage;

size_t RegisterMessage_Encode(const RegisterMessage *self, uint8_t *buffer, size_t buffer_size);
bool RegisterMessage_Decode(RegisterMessage *self, const uint8_t *payload, size_t payload_length);

size_t RegisterAckMessage_Encode(const RegisterAckMessage *self, uint8_t *buffer, size_t buffer_size);
bool RegisterAckMessage_Decode(RegisterAckMessage *self, const uint8_t *payload, size_t payload_length);
