#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OPEN_BODY_SIZE 4
#define OPEN_ACK_BODY_SIZE 8
#define CLOSE_BODY_SIZE 4

#define OPEN_STATUS_OK 0

typedef struct {
    uint32_t interface_id;
} OpenMessage;

typedef struct {
    uint8_t status;
    uint8_t channel;
    uint32_t interface_id;
} OpenAckMessage;

typedef struct {
    uint8_t channel;
} CloseMessage;

size_t OpenMessage_Encode(const OpenMessage *self, uint8_t *buffer, size_t buffer_size);
bool OpenMessage_Decode(OpenMessage *self, const uint8_t *payload, size_t payload_length);

size_t OpenAckMessage_Encode(const OpenAckMessage *self, uint8_t *buffer, size_t buffer_size);
bool OpenAckMessage_Decode(OpenAckMessage *self, const uint8_t *payload, size_t payload_length);

size_t CloseMessage_Encode(const CloseMessage *self, uint8_t *buffer, size_t buffer_size);
bool CloseMessage_Decode(CloseMessage *self, const uint8_t *payload, size_t payload_length);
