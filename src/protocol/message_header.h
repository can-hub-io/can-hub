#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MESSAGE_HEADER_SIZE 4

typedef enum tmessage_type_e {
    kMESSAGE_TYPE_HELLO = 0x01,
    kMESSAGE_TYPE_REGISTER = 0x02,
    kMESSAGE_TYPE_REGISTER_ACK = 0x03,
    kMESSAGE_TYPE_LIST = 0x04,
    kMESSAGE_TYPE_LIST_REPLY = 0x05,
    kMESSAGE_TYPE_OPEN = 0x06,
    kMESSAGE_TYPE_CLOSE = 0x07,
    kMESSAGE_TYPE_SUBSCRIBE = 0x08,
    kMESSAGE_TYPE_ERROR = 0x09,
    kMESSAGE_TYPE_OPEN_ACK = 0x0A,
    kMESSAGE_TYPE_FRAME = 0x40,
    kMESSAGE_TYPE_PING = 0x7F,
    kMESSAGE_TYPE_MAX,
} TMESSAGE_TYPE;

typedef struct {
    uint8_t type;
    uint8_t flags;
    uint16_t length;
} MessageHeader;

size_t MessageHeader_Encode(const MessageHeader *self, uint8_t *buffer, size_t buffer_size);
bool MessageHeader_Decode(MessageHeader *self, const uint8_t *buffer, size_t buffer_size);
