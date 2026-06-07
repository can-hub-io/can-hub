#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol/register_message.h"

#define ADMIN_STATUS_REPLY_BODY_SIZE 8
#define ADMIN_PEERS_BODY_SIZE 4
#define ADMIN_PEERS_REPLY_FIXED_FIELDS_SIZE 4
#define ADMIN_PEERS_REPLY_ENTRIES_MAX 16
#define ADMIN_PEERS_REPLY_ENTRY_SIZE 204
#define ADMIN_KICK_BODY_SIZE 128
#define ADMIN_KICK_REPLY_BODY_SIZE 4
#define ADMIN_PINS_BODY_SIZE 4
#define ADMIN_PINS_REPLY_FIXED_FIELDS_SIZE 4
#define ADMIN_PINS_REPLY_ENTRIES_MAX 16
#define ADMIN_PINS_REPLY_ENTRY_SIZE 196
#define ADMIN_FORGET_BODY_SIZE 128
#define ADMIN_FORGET_REPLY_BODY_SIZE 4

#define ADMIN_FINGERPRINT_HEX_SIZE 65
#define ADMIN_REPLY_FLAG_MORE (1u << 0)

#define ADMIN_STATUS_OK 0
#define ADMIN_STATUS_UNKNOWN_AGENT 1

typedef struct {
    uint16_t peer_count;
    uint16_t agent_count;
    uint16_t client_count;
    uint16_t interface_count;
} AdminStatusReplyMessage;

typedef struct {
    uint16_t offset;
} AdminPeersMessage;

typedef struct {
    uint32_t peer_id;
    uint8_t role;
    char agent_name[REGISTER_AGENT_NAME_SIZE];
    char fingerprint_hex[ADMIN_FINGERPRINT_HEX_SIZE];
} AdminPeersReplyEntry;

typedef struct {
    uint8_t count;
    uint8_t flags;
    AdminPeersReplyEntry entries[ADMIN_PEERS_REPLY_ENTRIES_MAX];
} AdminPeersReplyMessage;

typedef struct {
    char agent_name[REGISTER_AGENT_NAME_SIZE];
} AdminKickMessage;

typedef struct {
    uint8_t status;
} AdminKickReplyMessage;

typedef struct {
    uint16_t offset;
} AdminPinsMessage;

typedef struct {
    char agent_name[REGISTER_AGENT_NAME_SIZE];
    char fingerprint_hex[ADMIN_FINGERPRINT_HEX_SIZE];
} AdminPinsReplyEntry;

typedef struct {
    uint8_t count;
    uint8_t flags;
    AdminPinsReplyEntry entries[ADMIN_PINS_REPLY_ENTRIES_MAX];
} AdminPinsReplyMessage;

typedef struct {
    char agent_name[REGISTER_AGENT_NAME_SIZE];
} AdminForgetMessage;

typedef struct {
    uint8_t status;
} AdminForgetReplyMessage;

size_t AdminStatusMessage_Encode(uint8_t *buffer, size_t buffer_size);

size_t AdminStatusReplyMessage_Encode(const AdminStatusReplyMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminStatusReplyMessage_Decode(AdminStatusReplyMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminPeersMessage_Encode(const AdminPeersMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminPeersMessage_Decode(AdminPeersMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminPeersReplyMessage_Encode(const AdminPeersReplyMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminPeersReplyMessage_Decode(AdminPeersReplyMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminKickMessage_Encode(const AdminKickMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminKickMessage_Decode(AdminKickMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminKickReplyMessage_Encode(const AdminKickReplyMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminKickReplyMessage_Decode(AdminKickReplyMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminPinsMessage_Encode(const AdminPinsMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminPinsMessage_Decode(AdminPinsMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminPinsReplyMessage_Encode(const AdminPinsReplyMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminPinsReplyMessage_Decode(AdminPinsReplyMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminForgetMessage_Encode(const AdminForgetMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminForgetMessage_Decode(AdminForgetMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminForgetReplyMessage_Encode(const AdminForgetReplyMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminForgetReplyMessage_Decode(AdminForgetReplyMessage *self, const uint8_t *payload, size_t payload_length);
