#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol/register_message.h"

#define IFCONFIG_BODY_SIZE 24
#define IFCONFIG_REPLY_BODY_SIZE 20
#define ADMIN_IFCONFIG_BODY_SIZE 152
#define ADMIN_IFCONFIG_REPLY_BODY_SIZE 4

#define IFCONFIG_OP_SET_BITRATE 0
#define IFCONFIG_OP_UP 1
#define IFCONFIG_OP_DOWN 2

#define IFCONFIG_STATUS_OK 0
#define IFCONFIG_STATUS_UNKNOWN_INTERFACE 1
#define IFCONFIG_STATUS_APPLY_FAILED 2

#define ADMIN_IFCONFIG_STATUS_OK 0
#define ADMIN_IFCONFIG_STATUS_UNKNOWN_INTERFACE 1
#define ADMIN_IFCONFIG_STATUS_AGENT_UNREACHABLE 2
#define ADMIN_IFCONFIG_STATUS_APPLY_FAILED 3

typedef struct {
    char interface_name[REGISTER_INTERFACE_NAME_SIZE];
    uint8_t op;
    uint32_t bitrate;
} IfconfigMessage;

typedef struct {
    char interface_name[REGISTER_INTERFACE_NAME_SIZE];
    uint8_t status;
} IfconfigReplyMessage;

typedef struct {
    char agent_name[REGISTER_AGENT_NAME_SIZE];
    char interface_name[REGISTER_INTERFACE_NAME_SIZE];
    uint8_t op;
    uint32_t bitrate;
} AdminIfconfigMessage;

typedef struct {
    uint8_t status;
} AdminIfconfigReplyMessage;

size_t IfconfigMessage_Encode(const IfconfigMessage *self, uint8_t *buffer, size_t buffer_size);
bool IfconfigMessage_Decode(IfconfigMessage *self, const uint8_t *payload, size_t payload_length);

size_t IfconfigReplyMessage_Encode(const IfconfigReplyMessage *self, uint8_t *buffer, size_t buffer_size);
bool IfconfigReplyMessage_Decode(IfconfigReplyMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminIfconfigMessage_Encode(const AdminIfconfigMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminIfconfigMessage_Decode(AdminIfconfigMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminIfconfigReplyMessage_Encode(const AdminIfconfigReplyMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminIfconfigReplyMessage_Decode(AdminIfconfigReplyMessage *self, const uint8_t *payload, size_t payload_length);
