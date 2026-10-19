#include "protocol/admin_message.h"

#include <string.h>

#include "protocol/message_header.h"
#include "protocol/wire.h"

#define PEER_COUNT_OFFSET 0
#define AGENT_COUNT_OFFSET 2
#define CLIENT_COUNT_OFFSET 4
#define INTERFACE_COUNT_OFFSET 6
#define RECEIVE_OVERFLOWS_OFFSET 8
#define FRAMES_RECEIVED_OFFSET 12
#define FRAMES_FORWARDED_OFFSET 20
#define FRAMES_DROPPED_OFFSET 28
#define FRAMES_UNROUTABLE_OFFSET 36

#define OFFSET_OFFSET 0
#define COUNT_OFFSET 0
#define FLAGS_OFFSET 1
#define ENTRIES_OFFSET 4
#define STATUS_OFFSET 0
#define AGENT_NAME_OFFSET 0

#define PEER_ENTRY_PEER_ID_OFFSET 0
#define PEER_ENTRY_FRAMES_FORWARDED_OFFSET 4
#define PEER_ENTRY_FRAMES_DROPPED_OFFSET 8
#define PEER_ENTRY_ROLE_OFFSET 12
#define PEER_ENTRY_TRANSPORT_KIND_OFFSET 13
#define PEER_ENTRY_AGENT_NAME_OFFSET 16
#define PEER_ENTRY_FINGERPRINT_OFFSET 144
#define PEER_ENTRY_UPTIME_OFFSET 209
#define PEER_ENTRY_ORIGIN_OFFSET 213

#define PIN_ENTRY_AGENT_NAME_OFFSET 0
#define PIN_ENTRY_FINGERPRINT_OFFSET 128

#define PEER_ID_OFFSET 0
#define FILTER_NAME_OFFSET 4

#define AGENT_ENTRY_PEER_ID_OFFSET 0
#define AGENT_ENTRY_INTERFACE_COUNT_OFFSET 4
#define AGENT_ENTRY_AGENT_NAME_OFFSET 8
#define AGENT_ENTRY_FINGERPRINT_OFFSET 136

#define CLIENT_ENTRY_PEER_ID_OFFSET 0
#define CLIENT_ENTRY_INTERFACE_ID_OFFSET 4
#define CLIENT_ENTRY_CHANNEL_OFFSET 8
#define CLIENT_ENTRY_AGENT_NAME_OFFSET 12
#define CLIENT_ENTRY_INTERFACE_NAME_OFFSET 140
#define CLIENT_ENTRY_FRAMES_FORWARDED_OFFSET 156
#define CLIENT_ENTRY_FRAMES_DROPPED_OFFSET 160

#define ACL_AGENT_NAME_OFFSET 0
#define ACL_INTERFACE_NAME_OFFSET 128
#define ACL_FINGERPRINT_OFFSET 144
#define ACL_CAN_READ_OFFSET 209
#define ACL_CAN_WRITE_OFFSET 210
#define ACL_CAN_ADMIN_OFFSET 211

#define INTERFACE_ENTRY_ID_OFFSET 0
#define INTERFACE_ENTRY_SUBSCRIBERS_OFFSET 4
#define INTERFACE_ENTRY_MODE_OFFSET 5
#define INTERFACE_ENTRY_FRAMES_OFFSET 8
#define INTERFACE_ENTRY_AGENT_NAME_OFFSET 16
#define INTERFACE_ENTRY_INTERFACE_NAME_OFFSET 144
#define INTERFACE_ENTRY_TX_DROPPED_OFFSET 160

#define REJECTED_ENTRY_ROLE_OFFSET 0
#define REJECTED_ENTRY_REASON_OFFSET 1
#define REJECTED_ENTRY_ATTEMPTS_OFFSET 4
#define REJECTED_ENTRY_LAST_SEEN_OFFSET 8
#define REJECTED_ENTRY_NAME_OFFSET 12
#define REJECTED_ENTRY_FINGERPRINT_OFFSET 140
#define REJECTED_ENTRY_ORIGIN_OFFSET 205

#define BRIDGE_SOURCE_OFFSET 0
#define BRIDGE_DESTINATION_OFFSET 144
#define BRIDGE_ENDPOINT_INTERFACE_NAME_OFFSET 128
#define BRIDGE_FILTER_COUNT_OFFSET 288
#define BRIDGE_STATE_OFFSET 289
#define BRIDGE_FILTERS_OFFSET 292
#define BRIDGE_FILTER_MASK_OFFSET 4
#define BRIDGE_ENTRY_FORWARDED_OFFSET 424
#define BRIDGE_ENTRY_DROPPED_OFFSET 432

#define GROUP_NAME_OFFSET 0
#define GROUP_FILTER_COUNT_OFFSET 32
#define GROUP_FILTERS_OFFSET 36
#define GROUP_MEMBER_OFFSET 32
#define GROUP_ENTRY_MEMBER_COUNT_OFFSET 33
#define GROUP_ENTRY_FORWARDED_OFFSET 168
#define GROUP_ENTRY_DROPPED_OFFSET 176
#define GROUP_ENTRY_MEMBERS_OFFSET 184
#define GROUP_ENTRY_MEMBER_SIZE 216
#define GROUP_ENTRY_MEMBER_STATE_OFFSET 144
#define GROUP_ENTRY_MEMBER_DIRECTION_OFFSET 145
#define GROUP_ENTRY_MEMBER_FILTER_COUNT_OFFSET 146
#define GROUP_ENTRY_MEMBER_FILTERS_OFFSET 148
#define GROUP_MEMBER_DIRECTION_OFFSET 176
#define GROUP_MEMBER_FILTER_COUNT_OFFSET 177
#define GROUP_MEMBER_FILTERS_OFFSET 180

static size_t encodeFixedBody(uint8_t type, size_t body_size, uint8_t *buffer, size_t buffer_size, uint8_t **body);
static bool isNameTerminated(const char *agent_name);
static size_t encodeAgentName(uint8_t type, const char *agent_name, uint8_t *buffer, size_t buffer_size);
static bool decodeAgentName(char *agent_name, const uint8_t *payload, size_t payload_length, size_t body_size);
static size_t encodeStatusReply(uint8_t type, uint8_t status, uint8_t *buffer, size_t buffer_size);
static bool decodeStatusReply(uint8_t *status, const uint8_t *payload, size_t payload_length, size_t body_size);
static size_t encodeOffsetRequest(uint8_t type, uint16_t offset, uint8_t *buffer, size_t buffer_size);
static size_t encodeFilteredRequest(
    uint8_t type,
    uint16_t offset,
    const char *agent_name,
    uint8_t *buffer,
    size_t buffer_size
);
static bool decodeFilteredRequest(
    uint16_t *offset,
    char *agent_name,
    const uint8_t *payload,
    size_t payload_length
);
static void writePeerEntry(uint8_t *destination, const AdminPeersReplyEntry *entry);
static void readPeerEntry(const uint8_t *source, AdminPeersReplyEntry *entry);
static void writePinEntry(uint8_t *destination, const AdminPinsReplyEntry *entry);
static void readPinEntry(const uint8_t *source, AdminPinsReplyEntry *entry);
static void writeAgentEntry(uint8_t *destination, const AdminAgentsReplyEntry *entry);
static void readAgentEntry(const uint8_t *source, AdminAgentsReplyEntry *entry);
static void writeClientEntry(uint8_t *destination, const AdminClientsReplyEntry *entry);
static void readClientEntry(const uint8_t *source, AdminClientsReplyEntry *entry);
static void writeAclEntry(uint8_t *destination, const AdminAclListReplyEntry *entry);
static void readAclEntry(const uint8_t *source, AdminAclListReplyEntry *entry);
static void writeInterfaceEntry(uint8_t *destination, const AdminInterfacesReplyEntry *entry);
static void readInterfaceEntry(const uint8_t *source, AdminInterfacesReplyEntry *entry);
static bool isRejectedEntryTerminated(const AdminRejectedReplyEntry *entry);
static void writeRejectedEntry(uint8_t *destination, const AdminRejectedReplyEntry *entry);
static void readRejectedEntry(const uint8_t *source, AdminRejectedReplyEntry *entry);
static bool isBridgeEndpointValid(const AdminBridgeEndpoint *endpoint);
static void writeBridgeEndpoint(uint8_t *destination, const AdminBridgeEndpoint *endpoint);
static bool readBridgeEndpoint(const uint8_t *source, AdminBridgeEndpoint *endpoint);
static void writeBridgeFilters(uint8_t *destination, const CanFilter *filters, uint8_t filter_count);
static void readBridgeFilters(const uint8_t *source, CanFilter *filters, uint8_t filter_count);
static void writeBridgeEntry(uint8_t *destination, const AdminBridgeListReplyEntry *entry);
static bool readBridgeEntry(const uint8_t *source, AdminBridgeListReplyEntry *entry);
static bool isGroupNameValid(const char *name);
static bool readGroupName(const uint8_t *source, char *name);
static bool areGroupMembersValid(const AdminGroupListReplyEntry *entry);
static void writeGroupEntry(uint8_t *destination, const AdminGroupListReplyEntry *entry);
static bool readGroupEntry(const uint8_t *source, AdminGroupListReplyEntry *entry);

/* ---------- public ---------- */

size_t AdminStatusMessage_Encode(uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;

    return encodeFixedBody(kMESSAGE_TYPE_ADMIN_STATUS, 0, buffer, buffer_size, &body);
}

size_t AdminStatusReplyMessage_Encode(const AdminStatusReplyMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;
    size_t total_size;

    total_size = encodeFixedBody(kMESSAGE_TYPE_ADMIN_STATUS_REPLY, ADMIN_STATUS_REPLY_BODY_SIZE, buffer, buffer_size, &body);
    if (total_size == 0) {
        return 0;
    }

    Wire_WriteU16(body + PEER_COUNT_OFFSET, self->peer_count);
    Wire_WriteU16(body + AGENT_COUNT_OFFSET, self->agent_count);
    Wire_WriteU16(body + CLIENT_COUNT_OFFSET, self->client_count);
    Wire_WriteU16(body + INTERFACE_COUNT_OFFSET, self->interface_count);
    Wire_WriteU32(body + RECEIVE_OVERFLOWS_OFFSET, self->receive_overflows);
    Wire_WriteU64(body + FRAMES_RECEIVED_OFFSET, self->frames_received);
    Wire_WriteU64(body + FRAMES_FORWARDED_OFFSET, self->frames_forwarded);
    Wire_WriteU64(body + FRAMES_DROPPED_OFFSET, self->frames_dropped);
    Wire_WriteU64(body + FRAMES_UNROUTABLE_OFFSET, self->frames_unroutable);

    return total_size;
}

bool AdminStatusReplyMessage_Decode(AdminStatusReplyMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < ADMIN_STATUS_REPLY_BODY_SIZE) {
        return false;
    }

    self->peer_count = Wire_ReadU16(payload + PEER_COUNT_OFFSET);
    self->agent_count = Wire_ReadU16(payload + AGENT_COUNT_OFFSET);
    self->client_count = Wire_ReadU16(payload + CLIENT_COUNT_OFFSET);
    self->interface_count = Wire_ReadU16(payload + INTERFACE_COUNT_OFFSET);
    self->receive_overflows = Wire_ReadU32(payload + RECEIVE_OVERFLOWS_OFFSET);
    self->frames_received = Wire_ReadU64(payload + FRAMES_RECEIVED_OFFSET);
    self->frames_forwarded = Wire_ReadU64(payload + FRAMES_FORWARDED_OFFSET);
    self->frames_dropped = Wire_ReadU64(payload + FRAMES_DROPPED_OFFSET);
    self->frames_unroutable = Wire_ReadU64(payload + FRAMES_UNROUTABLE_OFFSET);

    return true;
}

size_t AdminPeersMessage_Encode(const AdminPeersMessage *self, uint8_t *buffer, size_t buffer_size)
{
    return encodeOffsetRequest(kMESSAGE_TYPE_ADMIN_PEERS, self->offset, buffer, buffer_size);
}

bool AdminPeersMessage_Decode(AdminPeersMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < ADMIN_PEERS_BODY_SIZE) {
        return false;
    }

    self->offset = Wire_ReadU16(payload + OFFSET_OFFSET);

    return true;
}

size_t AdminPeersReplyMessage_Encode(const AdminPeersReplyMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;
    size_t body_size;
    size_t total_size;
    uint8_t i;

    if (self->count > ADMIN_PEERS_REPLY_ENTRIES_MAX) {
        return 0;
    }
    for(i=0; i<self->count; i++) {
        if (!isNameTerminated(self->entries[i].agent_name)) {
            return 0;
        }
        if (self->entries[i].fingerprint_hex[ADMIN_FINGERPRINT_HEX_SIZE - 1] != '\0') {
            return 0;
        }
    }

    body_size = ADMIN_PEERS_REPLY_FIXED_FIELDS_SIZE + (size_t)self->count * ADMIN_PEERS_REPLY_ENTRY_SIZE;
    total_size = encodeFixedBody(kMESSAGE_TYPE_ADMIN_PEERS_REPLY, body_size, buffer, buffer_size, &body);
    if (total_size == 0) {
        return 0;
    }

    body[COUNT_OFFSET] = self->count;
    body[FLAGS_OFFSET] = self->flags;
    for(i=0; i<self->count; i++) {
        writePeerEntry(body + ENTRIES_OFFSET + i * ADMIN_PEERS_REPLY_ENTRY_SIZE, &self->entries[i]);
    }

    return total_size;
}

bool AdminPeersReplyMessage_Decode(AdminPeersReplyMessage *self, const uint8_t *payload, size_t payload_length)
{
    uint8_t i;

    if (payload_length < ADMIN_PEERS_REPLY_FIXED_FIELDS_SIZE) {
        return false;
    }

    self->count = payload[COUNT_OFFSET];
    self->flags = payload[FLAGS_OFFSET];
    if (self->count > ADMIN_PEERS_REPLY_ENTRIES_MAX) {
        return false;
    }
    if (payload_length < ADMIN_PEERS_REPLY_FIXED_FIELDS_SIZE + (size_t)self->count * ADMIN_PEERS_REPLY_ENTRY_SIZE) {
        return false;
    }

    for(i=0; i<self->count; i++) {
        readPeerEntry(payload + ENTRIES_OFFSET + i * ADMIN_PEERS_REPLY_ENTRY_SIZE, &self->entries[i]);
    }

    return true;
}

size_t AdminKickMessage_Encode(const AdminKickMessage *self, uint8_t *buffer, size_t buffer_size)
{
    return encodeAgentName(kMESSAGE_TYPE_ADMIN_KICK, self->agent_name, buffer, buffer_size);
}

bool AdminKickMessage_Decode(AdminKickMessage *self, const uint8_t *payload, size_t payload_length)
{
    return decodeAgentName(self->agent_name, payload, payload_length, ADMIN_KICK_BODY_SIZE);
}

size_t AdminKickReplyMessage_Encode(const AdminKickReplyMessage *self, uint8_t *buffer, size_t buffer_size)
{
    return encodeStatusReply(kMESSAGE_TYPE_ADMIN_KICK_REPLY, self->status, buffer, buffer_size);
}

bool AdminKickReplyMessage_Decode(AdminKickReplyMessage *self, const uint8_t *payload, size_t payload_length)
{
    return decodeStatusReply(&self->status, payload, payload_length, ADMIN_KICK_REPLY_BODY_SIZE);
}

size_t AdminPinsMessage_Encode(const AdminPinsMessage *self, uint8_t *buffer, size_t buffer_size)
{
    return encodeOffsetRequest(kMESSAGE_TYPE_ADMIN_PINS, self->offset, buffer, buffer_size);
}

bool AdminPinsMessage_Decode(AdminPinsMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < ADMIN_PINS_BODY_SIZE) {
        return false;
    }

    self->offset = Wire_ReadU16(payload + OFFSET_OFFSET);

    return true;
}

size_t AdminPinsReplyMessage_Encode(const AdminPinsReplyMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;
    size_t body_size;
    size_t total_size;
    uint8_t i;

    if (self->count > ADMIN_PINS_REPLY_ENTRIES_MAX) {
        return 0;
    }
    for(i=0; i<self->count; i++) {
        if (!isNameTerminated(self->entries[i].agent_name)) {
            return 0;
        }
        if (self->entries[i].fingerprint_hex[ADMIN_FINGERPRINT_HEX_SIZE - 1] != '\0') {
            return 0;
        }
    }

    body_size = ADMIN_PINS_REPLY_FIXED_FIELDS_SIZE + (size_t)self->count * ADMIN_PINS_REPLY_ENTRY_SIZE;
    total_size = encodeFixedBody(kMESSAGE_TYPE_ADMIN_PINS_REPLY, body_size, buffer, buffer_size, &body);
    if (total_size == 0) {
        return 0;
    }

    body[COUNT_OFFSET] = self->count;
    body[FLAGS_OFFSET] = self->flags;
    for(i=0; i<self->count; i++) {
        writePinEntry(body + ENTRIES_OFFSET + i * ADMIN_PINS_REPLY_ENTRY_SIZE, &self->entries[i]);
    }

    return total_size;
}

bool AdminPinsReplyMessage_Decode(AdminPinsReplyMessage *self, const uint8_t *payload, size_t payload_length)
{
    uint8_t i;

    if (payload_length < ADMIN_PINS_REPLY_FIXED_FIELDS_SIZE) {
        return false;
    }

    self->count = payload[COUNT_OFFSET];
    self->flags = payload[FLAGS_OFFSET];
    if (self->count > ADMIN_PINS_REPLY_ENTRIES_MAX) {
        return false;
    }
    if (payload_length < ADMIN_PINS_REPLY_FIXED_FIELDS_SIZE + (size_t)self->count * ADMIN_PINS_REPLY_ENTRY_SIZE) {
        return false;
    }

    for(i=0; i<self->count; i++) {
        readPinEntry(payload + ENTRIES_OFFSET + i * ADMIN_PINS_REPLY_ENTRY_SIZE, &self->entries[i]);
    }

    return true;
}

size_t AdminForgetMessage_Encode(const AdminForgetMessage *self, uint8_t *buffer, size_t buffer_size)
{
    return encodeAgentName(kMESSAGE_TYPE_ADMIN_FORGET, self->agent_name, buffer, buffer_size);
}

bool AdminForgetMessage_Decode(AdminForgetMessage *self, const uint8_t *payload, size_t payload_length)
{
    return decodeAgentName(self->agent_name, payload, payload_length, ADMIN_FORGET_BODY_SIZE);
}

size_t AdminForgetReplyMessage_Encode(const AdminForgetReplyMessage *self, uint8_t *buffer, size_t buffer_size)
{
    return encodeStatusReply(kMESSAGE_TYPE_ADMIN_FORGET_REPLY, self->status, buffer, buffer_size);
}

bool AdminForgetReplyMessage_Decode(AdminForgetReplyMessage *self, const uint8_t *payload, size_t payload_length)
{
    return decodeStatusReply(&self->status, payload, payload_length, ADMIN_FORGET_REPLY_BODY_SIZE);
}

size_t AdminKickPeerMessage_Encode(const AdminKickPeerMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;
    size_t total_size;

    total_size = encodeFixedBody(kMESSAGE_TYPE_ADMIN_KICK_PEER, ADMIN_KICK_PEER_BODY_SIZE, buffer, buffer_size, &body);
    if (total_size == 0) {
        return 0;
    }

    Wire_WriteU32(body + PEER_ID_OFFSET, self->peer_id);

    return total_size;
}

bool AdminKickPeerMessage_Decode(AdminKickPeerMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < ADMIN_KICK_PEER_BODY_SIZE) {
        return false;
    }

    self->peer_id = Wire_ReadU32(payload + PEER_ID_OFFSET);

    return true;
}

size_t AdminKickPeerReplyMessage_Encode(const AdminKickPeerReplyMessage *self, uint8_t *buffer, size_t buffer_size)
{
    return encodeStatusReply(kMESSAGE_TYPE_ADMIN_KICK_PEER_REPLY, self->status, buffer, buffer_size);
}

bool AdminKickPeerReplyMessage_Decode(AdminKickPeerReplyMessage *self, const uint8_t *payload, size_t payload_length)
{
    return decodeStatusReply(&self->status, payload, payload_length, ADMIN_KICK_PEER_REPLY_BODY_SIZE);
}

size_t AdminAgentsMessage_Encode(const AdminAgentsMessage *self, uint8_t *buffer, size_t buffer_size)
{
    return encodeFilteredRequest(kMESSAGE_TYPE_ADMIN_AGENTS, self->offset, self->agent_name, buffer, buffer_size);
}

bool AdminAgentsMessage_Decode(AdminAgentsMessage *self, const uint8_t *payload, size_t payload_length)
{
    return decodeFilteredRequest(&self->offset, self->agent_name, payload, payload_length);
}

size_t AdminAgentsReplyMessage_Encode(const AdminAgentsReplyMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;
    size_t body_size;
    size_t total_size;
    uint8_t i;

    if (self->count > ADMIN_AGENTS_REPLY_ENTRIES_MAX) {
        return 0;
    }
    for(i=0; i<self->count; i++) {
        if (!isNameTerminated(self->entries[i].agent_name)) {
            return 0;
        }
        if (self->entries[i].fingerprint_hex[ADMIN_FINGERPRINT_HEX_SIZE - 1] != '\0') {
            return 0;
        }
    }

    body_size = ADMIN_AGENTS_REPLY_FIXED_FIELDS_SIZE + (size_t)self->count * ADMIN_AGENTS_REPLY_ENTRY_SIZE;
    total_size = encodeFixedBody(kMESSAGE_TYPE_ADMIN_AGENTS_REPLY, body_size, buffer, buffer_size, &body);
    if (total_size == 0) {
        return 0;
    }

    body[COUNT_OFFSET] = self->count;
    body[FLAGS_OFFSET] = self->flags;
    for(i=0; i<self->count; i++) {
        writeAgentEntry(body + ENTRIES_OFFSET + i * ADMIN_AGENTS_REPLY_ENTRY_SIZE, &self->entries[i]);
    }

    return total_size;
}

bool AdminAgentsReplyMessage_Decode(AdminAgentsReplyMessage *self, const uint8_t *payload, size_t payload_length)
{
    uint8_t i;

    if (payload_length < ADMIN_AGENTS_REPLY_FIXED_FIELDS_SIZE) {
        return false;
    }

    self->count = payload[COUNT_OFFSET];
    self->flags = payload[FLAGS_OFFSET];
    if (self->count > ADMIN_AGENTS_REPLY_ENTRIES_MAX) {
        return false;
    }
    if (payload_length < ADMIN_AGENTS_REPLY_FIXED_FIELDS_SIZE + (size_t)self->count * ADMIN_AGENTS_REPLY_ENTRY_SIZE) {
        return false;
    }

    for(i=0; i<self->count; i++) {
        readAgentEntry(payload + ENTRIES_OFFSET + i * ADMIN_AGENTS_REPLY_ENTRY_SIZE, &self->entries[i]);
    }

    return true;
}

size_t AdminClientsMessage_Encode(const AdminClientsMessage *self, uint8_t *buffer, size_t buffer_size)
{
    return encodeFilteredRequest(kMESSAGE_TYPE_ADMIN_CLIENTS, self->offset, self->agent_name, buffer, buffer_size);
}

bool AdminClientsMessage_Decode(AdminClientsMessage *self, const uint8_t *payload, size_t payload_length)
{
    return decodeFilteredRequest(&self->offset, self->agent_name, payload, payload_length);
}

size_t AdminClientsReplyMessage_Encode(const AdminClientsReplyMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;
    size_t body_size;
    size_t total_size;
    uint8_t i;

    if (self->count > ADMIN_CLIENTS_REPLY_ENTRIES_MAX) {
        return 0;
    }
    for(i=0; i<self->count; i++) {
        if (!isNameTerminated(self->entries[i].agent_name)) {
            return 0;
        }
        if (self->entries[i].interface_name[REGISTER_INTERFACE_NAME_SIZE - 1] != '\0') {
            return 0;
        }
    }

    body_size = ADMIN_CLIENTS_REPLY_FIXED_FIELDS_SIZE + (size_t)self->count * ADMIN_CLIENTS_REPLY_ENTRY_SIZE;
    total_size = encodeFixedBody(kMESSAGE_TYPE_ADMIN_CLIENTS_REPLY, body_size, buffer, buffer_size, &body);
    if (total_size == 0) {
        return 0;
    }

    body[COUNT_OFFSET] = self->count;
    body[FLAGS_OFFSET] = self->flags;
    for(i=0; i<self->count; i++) {
        writeClientEntry(body + ENTRIES_OFFSET + i * ADMIN_CLIENTS_REPLY_ENTRY_SIZE, &self->entries[i]);
    }

    return total_size;
}

bool AdminClientsReplyMessage_Decode(AdminClientsReplyMessage *self, const uint8_t *payload, size_t payload_length)
{
    uint8_t i;

    if (payload_length < ADMIN_CLIENTS_REPLY_FIXED_FIELDS_SIZE) {
        return false;
    }

    self->count = payload[COUNT_OFFSET];
    self->flags = payload[FLAGS_OFFSET];
    if (self->count > ADMIN_CLIENTS_REPLY_ENTRIES_MAX) {
        return false;
    }
    if (payload_length < ADMIN_CLIENTS_REPLY_FIXED_FIELDS_SIZE + (size_t)self->count * ADMIN_CLIENTS_REPLY_ENTRY_SIZE) {
        return false;
    }

    for(i=0; i<self->count; i++) {
        readClientEntry(payload + ENTRIES_OFFSET + i * ADMIN_CLIENTS_REPLY_ENTRY_SIZE, &self->entries[i]);
    }

    return true;
}

size_t AdminPinAddMessage_Encode(const AdminPinAddMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;
    size_t total_size;

    if (!isNameTerminated(self->agent_name) || self->agent_name[0] == '\0') {
        return 0;
    }
    if (self->fingerprint_hex[ADMIN_FINGERPRINT_HEX_SIZE - 1] != '\0' || self->fingerprint_hex[0] == '\0') {
        return 0;
    }

    total_size = encodeFixedBody(kMESSAGE_TYPE_ADMIN_PIN_ADD, ADMIN_PIN_ADD_BODY_SIZE, buffer, buffer_size, &body);
    if (total_size == 0) {
        return 0;
    }

    memcpy(body + PIN_ENTRY_AGENT_NAME_OFFSET, self->agent_name, strlen(self->agent_name));
    memcpy(body + PIN_ENTRY_FINGERPRINT_OFFSET, self->fingerprint_hex, strlen(self->fingerprint_hex));

    return total_size;
}

bool AdminPinAddMessage_Decode(AdminPinAddMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < ADMIN_PIN_ADD_BODY_SIZE) {
        return false;
    }

    memcpy(self->agent_name, payload + PIN_ENTRY_AGENT_NAME_OFFSET, REGISTER_AGENT_NAME_SIZE);
    self->agent_name[REGISTER_AGENT_NAME_SIZE - 1] = '\0';
    memcpy(self->fingerprint_hex, payload + PIN_ENTRY_FINGERPRINT_OFFSET, ADMIN_FINGERPRINT_HEX_SIZE);
    self->fingerprint_hex[ADMIN_FINGERPRINT_HEX_SIZE - 1] = '\0';

    return self->agent_name[0] != '\0' && self->fingerprint_hex[0] != '\0';
}

size_t AdminPinAddReplyMessage_Encode(const AdminPinAddReplyMessage *self, uint8_t *buffer, size_t buffer_size)
{
    return encodeStatusReply(kMESSAGE_TYPE_ADMIN_PIN_ADD_REPLY, self->status, buffer, buffer_size);
}

bool AdminPinAddReplyMessage_Decode(AdminPinAddReplyMessage *self, const uint8_t *payload, size_t payload_length)
{
    return decodeStatusReply(&self->status, payload, payload_length, ADMIN_PIN_ADD_REPLY_BODY_SIZE);
}

size_t AdminAclSetMessage_Encode(const AdminAclSetMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;
    size_t total_size;

    if (!isNameTerminated(self->agent_name) || self->agent_name[0] == '\0') {
        return 0;
    }
    if (self->interface_name[REGISTER_INTERFACE_NAME_SIZE - 1] != '\0' || self->interface_name[0] == '\0') {
        return 0;
    }
    if (self->fingerprint_hex[ADMIN_FINGERPRINT_HEX_SIZE - 1] != '\0' || self->fingerprint_hex[0] == '\0') {
        return 0;
    }

    total_size = encodeFixedBody(kMESSAGE_TYPE_ADMIN_ACL_SET, ADMIN_ACL_SET_BODY_SIZE, buffer, buffer_size, &body);
    if (total_size == 0) {
        return 0;
    }

    memcpy(body + ACL_AGENT_NAME_OFFSET, self->agent_name, strlen(self->agent_name));
    memcpy(body + ACL_INTERFACE_NAME_OFFSET, self->interface_name, strlen(self->interface_name));
    memcpy(body + ACL_FINGERPRINT_OFFSET, self->fingerprint_hex, strlen(self->fingerprint_hex));
    body[ACL_CAN_READ_OFFSET] = self->can_read;
    body[ACL_CAN_WRITE_OFFSET] = self->can_write;
    body[ACL_CAN_ADMIN_OFFSET] = self->can_admin;

    return total_size;
}

bool AdminAclSetMessage_Decode(AdminAclSetMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < ADMIN_ACL_SET_BODY_SIZE) {
        return false;
    }

    memcpy(self->agent_name, payload + ACL_AGENT_NAME_OFFSET, REGISTER_AGENT_NAME_SIZE);
    self->agent_name[REGISTER_AGENT_NAME_SIZE - 1] = '\0';
    memcpy(self->interface_name, payload + ACL_INTERFACE_NAME_OFFSET, REGISTER_INTERFACE_NAME_SIZE);
    self->interface_name[REGISTER_INTERFACE_NAME_SIZE - 1] = '\0';
    memcpy(self->fingerprint_hex, payload + ACL_FINGERPRINT_OFFSET, ADMIN_FINGERPRINT_HEX_SIZE);
    self->fingerprint_hex[ADMIN_FINGERPRINT_HEX_SIZE - 1] = '\0';
    self->can_read = payload[ACL_CAN_READ_OFFSET];
    self->can_write = payload[ACL_CAN_WRITE_OFFSET];
    self->can_admin = payload[ACL_CAN_ADMIN_OFFSET];

    return self->agent_name[0] != '\0' && self->interface_name[0] != '\0' && self->fingerprint_hex[0] != '\0';
}

size_t AdminAclSetReplyMessage_Encode(const AdminAclSetReplyMessage *self, uint8_t *buffer, size_t buffer_size)
{
    return encodeStatusReply(kMESSAGE_TYPE_ADMIN_ACL_SET_REPLY, self->status, buffer, buffer_size);
}

bool AdminAclSetReplyMessage_Decode(AdminAclSetReplyMessage *self, const uint8_t *payload, size_t payload_length)
{
    return decodeStatusReply(&self->status, payload, payload_length, ADMIN_ACL_SET_REPLY_BODY_SIZE);
}

size_t AdminAclRevokeMessage_Encode(const AdminAclRevokeMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;
    size_t total_size;

    if (!isNameTerminated(self->agent_name) || self->agent_name[0] == '\0') {
        return 0;
    }
    if (self->interface_name[REGISTER_INTERFACE_NAME_SIZE - 1] != '\0' || self->interface_name[0] == '\0') {
        return 0;
    }
    if (self->fingerprint_hex[ADMIN_FINGERPRINT_HEX_SIZE - 1] != '\0' || self->fingerprint_hex[0] == '\0') {
        return 0;
    }

    total_size = encodeFixedBody(kMESSAGE_TYPE_ADMIN_ACL_REVOKE, ADMIN_ACL_REVOKE_BODY_SIZE, buffer, buffer_size, &body);
    if (total_size == 0) {
        return 0;
    }

    memcpy(body + ACL_AGENT_NAME_OFFSET, self->agent_name, strlen(self->agent_name));
    memcpy(body + ACL_INTERFACE_NAME_OFFSET, self->interface_name, strlen(self->interface_name));
    memcpy(body + ACL_FINGERPRINT_OFFSET, self->fingerprint_hex, strlen(self->fingerprint_hex));

    return total_size;
}

bool AdminAclRevokeMessage_Decode(AdminAclRevokeMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < ADMIN_ACL_REVOKE_BODY_SIZE) {
        return false;
    }

    memcpy(self->agent_name, payload + ACL_AGENT_NAME_OFFSET, REGISTER_AGENT_NAME_SIZE);
    self->agent_name[REGISTER_AGENT_NAME_SIZE - 1] = '\0';
    memcpy(self->interface_name, payload + ACL_INTERFACE_NAME_OFFSET, REGISTER_INTERFACE_NAME_SIZE);
    self->interface_name[REGISTER_INTERFACE_NAME_SIZE - 1] = '\0';
    memcpy(self->fingerprint_hex, payload + ACL_FINGERPRINT_OFFSET, ADMIN_FINGERPRINT_HEX_SIZE);
    self->fingerprint_hex[ADMIN_FINGERPRINT_HEX_SIZE - 1] = '\0';

    return self->agent_name[0] != '\0' && self->interface_name[0] != '\0' && self->fingerprint_hex[0] != '\0';
}

size_t AdminAclRevokeReplyMessage_Encode(const AdminAclRevokeReplyMessage *self, uint8_t *buffer, size_t buffer_size)
{
    return encodeStatusReply(kMESSAGE_TYPE_ADMIN_ACL_REVOKE_REPLY, self->status, buffer, buffer_size);
}

bool AdminAclRevokeReplyMessage_Decode(AdminAclRevokeReplyMessage *self, const uint8_t *payload, size_t payload_length)
{
    return decodeStatusReply(&self->status, payload, payload_length, ADMIN_ACL_REVOKE_REPLY_BODY_SIZE);
}

size_t AdminAclListMessage_Encode(const AdminAclListMessage *self, uint8_t *buffer, size_t buffer_size)
{
    return encodeOffsetRequest(kMESSAGE_TYPE_ADMIN_ACL_LIST, self->offset, buffer, buffer_size);
}

bool AdminAclListMessage_Decode(AdminAclListMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < ADMIN_ACL_LIST_BODY_SIZE) {
        return false;
    }

    self->offset = Wire_ReadU16(payload + OFFSET_OFFSET);

    return true;
}

size_t AdminAclListReplyMessage_Encode(const AdminAclListReplyMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;
    size_t body_size;
    size_t total_size;
    uint8_t i;

    if (self->count > ADMIN_ACL_LIST_REPLY_ENTRIES_MAX) {
        return 0;
    }
    for(i=0; i<self->count; i++) {
        if (!isNameTerminated(self->entries[i].agent_name)) {
            return 0;
        }
        if (self->entries[i].interface_name[REGISTER_INTERFACE_NAME_SIZE - 1] != '\0') {
            return 0;
        }
        if (self->entries[i].fingerprint_hex[ADMIN_FINGERPRINT_HEX_SIZE - 1] != '\0') {
            return 0;
        }
    }

    body_size = ADMIN_ACL_LIST_REPLY_FIXED_FIELDS_SIZE + (size_t)self->count * ADMIN_ACL_LIST_REPLY_ENTRY_SIZE;
    total_size = encodeFixedBody(kMESSAGE_TYPE_ADMIN_ACL_LIST_REPLY, body_size, buffer, buffer_size, &body);
    if (total_size == 0) {
        return 0;
    }

    body[COUNT_OFFSET] = self->count;
    body[FLAGS_OFFSET] = self->flags;
    for(i=0; i<self->count; i++) {
        writeAclEntry(body + ENTRIES_OFFSET + i * ADMIN_ACL_LIST_REPLY_ENTRY_SIZE, &self->entries[i]);
    }

    return total_size;
}

bool AdminAclListReplyMessage_Decode(AdminAclListReplyMessage *self, const uint8_t *payload, size_t payload_length)
{
    uint8_t i;

    if (payload_length < ADMIN_ACL_LIST_REPLY_FIXED_FIELDS_SIZE) {
        return false;
    }

    self->count = payload[COUNT_OFFSET];
    self->flags = payload[FLAGS_OFFSET];
    if (self->count > ADMIN_ACL_LIST_REPLY_ENTRIES_MAX) {
        return false;
    }
    if (payload_length < ADMIN_ACL_LIST_REPLY_FIXED_FIELDS_SIZE + (size_t)self->count * ADMIN_ACL_LIST_REPLY_ENTRY_SIZE) {
        return false;
    }

    for(i=0; i<self->count; i++) {
        readAclEntry(payload + ENTRIES_OFFSET + i * ADMIN_ACL_LIST_REPLY_ENTRY_SIZE, &self->entries[i]);
    }

    return true;
}

size_t AdminInterfacesMessage_Encode(const AdminInterfacesMessage *self, uint8_t *buffer, size_t buffer_size)
{
    return encodeOffsetRequest(kMESSAGE_TYPE_ADMIN_INTERFACES, self->offset, buffer, buffer_size);
}

bool AdminInterfacesMessage_Decode(AdminInterfacesMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < ADMIN_INTERFACES_BODY_SIZE) {
        return false;
    }

    self->offset = Wire_ReadU16(payload + OFFSET_OFFSET);

    return true;
}

size_t AdminInterfacesReplyMessage_Encode(const AdminInterfacesReplyMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;
    size_t body_size;
    size_t total_size;
    uint8_t i;

    if (self->count > ADMIN_INTERFACES_REPLY_ENTRIES_MAX) {
        return 0;
    }
    for(i=0; i<self->count; i++) {
        if (!isNameTerminated(self->entries[i].agent_name)) {
            return 0;
        }
        if (self->entries[i].interface_name[REGISTER_INTERFACE_NAME_SIZE - 1] != '\0') {
            return 0;
        }
    }

    body_size = ADMIN_INTERFACES_REPLY_FIXED_FIELDS_SIZE + (size_t)self->count * ADMIN_INTERFACES_REPLY_ENTRY_SIZE;
    total_size = encodeFixedBody(kMESSAGE_TYPE_ADMIN_INTERFACES_REPLY, body_size, buffer, buffer_size, &body);
    if (total_size == 0) {
        return 0;
    }

    body[COUNT_OFFSET] = self->count;
    body[FLAGS_OFFSET] = self->flags;
    for(i=0; i<self->count; i++) {
        writeInterfaceEntry(body + ENTRIES_OFFSET + i * ADMIN_INTERFACES_REPLY_ENTRY_SIZE, &self->entries[i]);
    }

    return total_size;
}

bool AdminInterfacesReplyMessage_Decode(AdminInterfacesReplyMessage *self, const uint8_t *payload, size_t payload_length)
{
    uint8_t i;

    if (payload_length < ADMIN_INTERFACES_REPLY_FIXED_FIELDS_SIZE) {
        return false;
    }

    self->count = payload[COUNT_OFFSET];
    self->flags = payload[FLAGS_OFFSET];
    if (self->count > ADMIN_INTERFACES_REPLY_ENTRIES_MAX) {
        return false;
    }
    if (payload_length < ADMIN_INTERFACES_REPLY_FIXED_FIELDS_SIZE + (size_t)self->count * ADMIN_INTERFACES_REPLY_ENTRY_SIZE) {
        return false;
    }

    for(i=0; i<self->count; i++) {
        readInterfaceEntry(payload + ENTRIES_OFFSET + i * ADMIN_INTERFACES_REPLY_ENTRY_SIZE, &self->entries[i]);
    }

    return true;
}

size_t AdminRejectedMessage_Encode(const AdminRejectedMessage *self, uint8_t *buffer, size_t buffer_size)
{
    return encodeOffsetRequest(kMESSAGE_TYPE_ADMIN_REJECTED, self->offset, buffer, buffer_size);
}

bool AdminRejectedMessage_Decode(AdminRejectedMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < ADMIN_REJECTED_BODY_SIZE) {
        return false;
    }

    self->offset = Wire_ReadU16(payload + OFFSET_OFFSET);

    return true;
}

size_t AdminRejectedReplyMessage_Encode(const AdminRejectedReplyMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;
    size_t body_size;
    size_t total_size;
    uint8_t i;

    if (self->count > ADMIN_REJECTED_REPLY_ENTRIES_MAX) {
        return 0;
    }
    for(i=0; i<self->count; i++) {
        if (!isRejectedEntryTerminated(&self->entries[i])) {
            return 0;
        }
    }

    body_size = ADMIN_REJECTED_REPLY_FIXED_FIELDS_SIZE + (size_t)self->count * ADMIN_REJECTED_REPLY_ENTRY_SIZE;
    total_size = encodeFixedBody(kMESSAGE_TYPE_ADMIN_REJECTED_REPLY, body_size, buffer, buffer_size, &body);
    if (total_size == 0) {
        return 0;
    }

    body[COUNT_OFFSET] = self->count;
    body[FLAGS_OFFSET] = self->flags;
    for(i=0; i<self->count; i++) {
        writeRejectedEntry(body + ENTRIES_OFFSET + i * ADMIN_REJECTED_REPLY_ENTRY_SIZE, &self->entries[i]);
    }

    return total_size;
}

bool AdminRejectedReplyMessage_Decode(AdminRejectedReplyMessage *self, const uint8_t *payload, size_t payload_length)
{
    uint8_t i;

    if (payload_length < ADMIN_REJECTED_REPLY_FIXED_FIELDS_SIZE) {
        return false;
    }

    self->count = payload[COUNT_OFFSET];
    self->flags = payload[FLAGS_OFFSET];
    if (self->count > ADMIN_REJECTED_REPLY_ENTRIES_MAX) {
        return false;
    }
    if (payload_length < ADMIN_REJECTED_REPLY_FIXED_FIELDS_SIZE + (size_t)self->count * ADMIN_REJECTED_REPLY_ENTRY_SIZE) {
        return false;
    }

    for(i=0; i<self->count; i++) {
        readRejectedEntry(payload + ENTRIES_OFFSET + i * ADMIN_REJECTED_REPLY_ENTRY_SIZE, &self->entries[i]);
    }

    return true;
}

const char *AdminBridgeStatus_Name(uint8_t status)
{
    switch (status) {
        case ADMIN_BRIDGE_STATUS_OK:
            return "ok";
        case ADMIN_BRIDGE_STATUS_INVALID:
            return "a bridge needs two different interfaces";
        case ADMIN_BRIDGE_STATUS_FULL:
            return "the hub holds no more bridges";
        case ADMIN_BRIDGE_STATUS_STORE_FAILED:
            return "could not store the bridge";
        case ADMIN_BRIDGE_STATUS_UNKNOWN:
            return "no such bridge";
        default:
            return "unknown status";
    }
}

size_t AdminBridgeSetMessage_Encode(const AdminBridgeSetMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;
    size_t total_size;

    if (!isBridgeEndpointValid(&self->source) || !isBridgeEndpointValid(&self->destination)) {
        return 0;
    }
    if (self->filter_count > SUBSCRIBE_FILTERS_MAX) {
        return 0;
    }

    total_size = encodeFixedBody(kMESSAGE_TYPE_ADMIN_BRIDGE_SET, ADMIN_BRIDGE_SET_BODY_SIZE, buffer, buffer_size, &body);
    if (total_size == 0) {
        return 0;
    }

    writeBridgeEndpoint(body + BRIDGE_SOURCE_OFFSET, &self->source);
    writeBridgeEndpoint(body + BRIDGE_DESTINATION_OFFSET, &self->destination);
    body[BRIDGE_FILTER_COUNT_OFFSET] = self->filter_count;
    writeBridgeFilters(body + BRIDGE_FILTERS_OFFSET, self->filters, self->filter_count);

    return total_size;
}

bool AdminBridgeSetMessage_Decode(AdminBridgeSetMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < ADMIN_BRIDGE_SET_BODY_SIZE) {
        return false;
    }
    if (!readBridgeEndpoint(payload + BRIDGE_SOURCE_OFFSET, &self->source)) {
        return false;
    }
    if (!readBridgeEndpoint(payload + BRIDGE_DESTINATION_OFFSET, &self->destination)) {
        return false;
    }

    self->filter_count = payload[BRIDGE_FILTER_COUNT_OFFSET];
    if (self->filter_count > SUBSCRIBE_FILTERS_MAX) {
        return false;
    }
    readBridgeFilters(payload + BRIDGE_FILTERS_OFFSET, self->filters, self->filter_count);

    return true;
}

size_t AdminBridgeSetReplyMessage_Encode(const AdminBridgeSetReplyMessage *self, uint8_t *buffer, size_t buffer_size)
{
    return encodeStatusReply(kMESSAGE_TYPE_ADMIN_BRIDGE_SET_REPLY, self->status, buffer, buffer_size);
}

bool AdminBridgeSetReplyMessage_Decode(AdminBridgeSetReplyMessage *self, const uint8_t *payload, size_t payload_length)
{
    return decodeStatusReply(&self->status, payload, payload_length, ADMIN_BRIDGE_SET_REPLY_BODY_SIZE);
}

size_t AdminBridgeDeleteMessage_Encode(const AdminBridgeDeleteMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;
    size_t total_size;

    if (!isBridgeEndpointValid(&self->source) || !isBridgeEndpointValid(&self->destination)) {
        return 0;
    }

    total_size = encodeFixedBody(kMESSAGE_TYPE_ADMIN_BRIDGE_DELETE, ADMIN_BRIDGE_DELETE_BODY_SIZE, buffer, buffer_size, &body);
    if (total_size == 0) {
        return 0;
    }

    writeBridgeEndpoint(body + BRIDGE_SOURCE_OFFSET, &self->source);
    writeBridgeEndpoint(body + BRIDGE_DESTINATION_OFFSET, &self->destination);

    return total_size;
}

bool AdminBridgeDeleteMessage_Decode(AdminBridgeDeleteMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < ADMIN_BRIDGE_DELETE_BODY_SIZE) {
        return false;
    }

    return readBridgeEndpoint(payload + BRIDGE_SOURCE_OFFSET, &self->source)
        && readBridgeEndpoint(payload + BRIDGE_DESTINATION_OFFSET, &self->destination);
}

size_t AdminBridgeDeleteReplyMessage_Encode(const AdminBridgeDeleteReplyMessage *self, uint8_t *buffer, size_t buffer_size)
{
    return encodeStatusReply(kMESSAGE_TYPE_ADMIN_BRIDGE_DELETE_REPLY, self->status, buffer, buffer_size);
}

bool AdminBridgeDeleteReplyMessage_Decode(AdminBridgeDeleteReplyMessage *self, const uint8_t *payload, size_t payload_length)
{
    return decodeStatusReply(&self->status, payload, payload_length, ADMIN_BRIDGE_DELETE_REPLY_BODY_SIZE);
}

size_t AdminBridgeListMessage_Encode(const AdminBridgeListMessage *self, uint8_t *buffer, size_t buffer_size)
{
    return encodeOffsetRequest(kMESSAGE_TYPE_ADMIN_BRIDGE_LIST, self->offset, buffer, buffer_size);
}

bool AdminBridgeListMessage_Decode(AdminBridgeListMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < ADMIN_BRIDGE_LIST_BODY_SIZE) {
        return false;
    }

    self->offset = Wire_ReadU16(payload + OFFSET_OFFSET);

    return true;
}

size_t AdminBridgeListReplyMessage_Encode(const AdminBridgeListReplyMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;
    size_t body_size;
    size_t total_size;
    uint8_t i;

    if (self->count > ADMIN_BRIDGE_LIST_REPLY_ENTRIES_MAX) {
        return 0;
    }
    for(i=0; i<self->count; i++) {
        if (!isBridgeEndpointValid(&self->entries[i].source) || !isBridgeEndpointValid(&self->entries[i].destination)) {
            return 0;
        }
        if (self->entries[i].filter_count > SUBSCRIBE_FILTERS_MAX) {
            return 0;
        }
    }

    body_size = ADMIN_BRIDGE_LIST_REPLY_FIXED_FIELDS_SIZE + (size_t)self->count * ADMIN_BRIDGE_LIST_REPLY_ENTRY_SIZE;
    total_size = encodeFixedBody(kMESSAGE_TYPE_ADMIN_BRIDGE_LIST_REPLY, body_size, buffer, buffer_size, &body);
    if (total_size == 0) {
        return 0;
    }

    body[COUNT_OFFSET] = self->count;
    body[FLAGS_OFFSET] = self->flags;
    for(i=0; i<self->count; i++) {
        writeBridgeEntry(body + ENTRIES_OFFSET + i * ADMIN_BRIDGE_LIST_REPLY_ENTRY_SIZE, &self->entries[i]);
    }

    return total_size;
}

bool AdminBridgeListReplyMessage_Decode(AdminBridgeListReplyMessage *self, const uint8_t *payload, size_t payload_length)
{
    uint8_t i;

    if (payload_length < ADMIN_BRIDGE_LIST_REPLY_FIXED_FIELDS_SIZE) {
        return false;
    }

    self->count = payload[COUNT_OFFSET];
    self->flags = payload[FLAGS_OFFSET];
    if (self->count > ADMIN_BRIDGE_LIST_REPLY_ENTRIES_MAX) {
        return false;
    }
    if (payload_length < ADMIN_BRIDGE_LIST_REPLY_FIXED_FIELDS_SIZE + (size_t)self->count * ADMIN_BRIDGE_LIST_REPLY_ENTRY_SIZE) {
        return false;
    }

    for(i=0; i<self->count; i++) {
        if (!readBridgeEntry(payload + ENTRIES_OFFSET + i * ADMIN_BRIDGE_LIST_REPLY_ENTRY_SIZE, &self->entries[i])) {
            return false;
        }
    }

    return true;
}

size_t AdminGroupSetMessage_Encode(const AdminGroupSetMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;
    size_t total_size;

    if (!isGroupNameValid(self->name) || self->filter_count > SUBSCRIBE_FILTERS_MAX) {
        return 0;
    }

    total_size = encodeFixedBody(kMESSAGE_TYPE_ADMIN_GROUP_SET, ADMIN_GROUP_SET_BODY_SIZE, buffer, buffer_size, &body);
    if (total_size == 0) {
        return 0;
    }

    memcpy(body + GROUP_NAME_OFFSET, self->name, strlen(self->name));
    body[GROUP_FILTER_COUNT_OFFSET] = self->filter_count;
    writeBridgeFilters(body + GROUP_FILTERS_OFFSET, self->filters, self->filter_count);

    return total_size;
}

bool AdminGroupSetMessage_Decode(AdminGroupSetMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < ADMIN_GROUP_SET_BODY_SIZE || !readGroupName(payload + GROUP_NAME_OFFSET, self->name)) {
        return false;
    }

    self->filter_count = payload[GROUP_FILTER_COUNT_OFFSET];
    if (self->filter_count > SUBSCRIBE_FILTERS_MAX) {
        return false;
    }
    readBridgeFilters(payload + GROUP_FILTERS_OFFSET, self->filters, self->filter_count);

    return true;
}

size_t AdminGroupDeleteMessage_Encode(const AdminGroupDeleteMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;
    size_t total_size;

    if (!isGroupNameValid(self->name)) {
        return 0;
    }

    total_size = encodeFixedBody(kMESSAGE_TYPE_ADMIN_GROUP_DELETE, ADMIN_GROUP_DELETE_BODY_SIZE, buffer, buffer_size, &body);
    if (total_size == 0) {
        return 0;
    }

    memcpy(body + GROUP_NAME_OFFSET, self->name, strlen(self->name));

    return total_size;
}

bool AdminGroupDeleteMessage_Decode(AdminGroupDeleteMessage *self, const uint8_t *payload, size_t payload_length)
{
    return payload_length >= ADMIN_GROUP_DELETE_BODY_SIZE && readGroupName(payload + GROUP_NAME_OFFSET, self->name);
}

size_t AdminGroupMemberMessage_Encode(uint8_t type, const AdminGroupMemberMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;
    size_t total_size;

    if (type != kMESSAGE_TYPE_ADMIN_GROUP_JOIN && type != kMESSAGE_TYPE_ADMIN_GROUP_LEAVE) {
        return 0;
    }
    if (!isGroupNameValid(self->name) || !isBridgeEndpointValid(&self->member)) {
        return 0;
    }
    if (self->direction > ADMIN_GROUP_MEMBER_RECEIVES || self->filter_count > ADMIN_GROUP_MEMBER_FILTERS_MAX) {
        return 0;
    }

    total_size = encodeFixedBody(type, ADMIN_GROUP_MEMBER_BODY_SIZE, buffer, buffer_size, &body);
    if (total_size == 0) {
        return 0;
    }

    memcpy(body + GROUP_NAME_OFFSET, self->name, strlen(self->name));
    writeBridgeEndpoint(body + GROUP_MEMBER_OFFSET, &self->member);
    body[GROUP_MEMBER_DIRECTION_OFFSET] = self->direction;
    body[GROUP_MEMBER_FILTER_COUNT_OFFSET] = self->filter_count;
    writeBridgeFilters(body + GROUP_MEMBER_FILTERS_OFFSET, self->filters, self->filter_count);

    return total_size;
}

bool AdminGroupMemberMessage_Decode(AdminGroupMemberMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < ADMIN_GROUP_MEMBER_BODY_SIZE || !readGroupName(payload + GROUP_NAME_OFFSET, self->name)) {
        return false;
    }

    if (!readBridgeEndpoint(payload + GROUP_MEMBER_OFFSET, &self->member)) {
        return false;
    }

    self->direction = payload[GROUP_MEMBER_DIRECTION_OFFSET];
    self->filter_count = payload[GROUP_MEMBER_FILTER_COUNT_OFFSET];
    if (self->direction > ADMIN_GROUP_MEMBER_RECEIVES || self->filter_count > ADMIN_GROUP_MEMBER_FILTERS_MAX) {
        return false;
    }
    readBridgeFilters(payload + GROUP_MEMBER_FILTERS_OFFSET, self->filters, self->filter_count);

    return true;
}

size_t AdminGroupReplyMessage_Encode(uint8_t type, const AdminGroupReplyMessage *self, uint8_t *buffer, size_t buffer_size)
{
    return encodeStatusReply(type, self->status, buffer, buffer_size);
}

bool AdminGroupReplyMessage_Decode(AdminGroupReplyMessage *self, const uint8_t *payload, size_t payload_length)
{
    return decodeStatusReply(&self->status, payload, payload_length, ADMIN_GROUP_REPLY_BODY_SIZE);
}

size_t AdminGroupListMessage_Encode(const AdminGroupListMessage *self, uint8_t *buffer, size_t buffer_size)
{
    return encodeOffsetRequest(kMESSAGE_TYPE_ADMIN_GROUP_LIST, self->offset, buffer, buffer_size);
}

bool AdminGroupListMessage_Decode(AdminGroupListMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < ADMIN_GROUP_LIST_BODY_SIZE) {
        return false;
    }

    self->offset = Wire_ReadU16(payload + OFFSET_OFFSET);

    return true;
}

size_t AdminGroupListReplyMessage_Encode(const AdminGroupListReplyMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;
    size_t body_size;
    size_t total_size;
    uint8_t i;

    if (self->count > ADMIN_GROUP_LIST_REPLY_ENTRIES_MAX) {
        return 0;
    }
    for(i=0; i<self->count; i++) {
        if (!isGroupNameValid(self->entries[i].name)) {
            return 0;
        }
        if (self->entries[i].filter_count > SUBSCRIBE_FILTERS_MAX || self->entries[i].member_count > ADMIN_GROUP_MEMBERS_MAX) {
            return 0;
        }
        if (!areGroupMembersValid(&self->entries[i])) {
            return 0;
        }
    }

    body_size = ADMIN_GROUP_LIST_REPLY_FIXED_FIELDS_SIZE + (size_t)self->count * ADMIN_GROUP_LIST_REPLY_ENTRY_SIZE;
    total_size = encodeFixedBody(kMESSAGE_TYPE_ADMIN_GROUP_LIST_REPLY, body_size, buffer, buffer_size, &body);
    if (total_size == 0) {
        return 0;
    }

    body[COUNT_OFFSET] = self->count;
    body[FLAGS_OFFSET] = self->flags;
    for(i=0; i<self->count; i++) {
        writeGroupEntry(body + ENTRIES_OFFSET + i * ADMIN_GROUP_LIST_REPLY_ENTRY_SIZE, &self->entries[i]);
    }

    return total_size;
}

bool AdminGroupListReplyMessage_Decode(AdminGroupListReplyMessage *self, const uint8_t *payload, size_t payload_length)
{
    uint8_t i;

    if (payload_length < ADMIN_GROUP_LIST_REPLY_FIXED_FIELDS_SIZE) {
        return false;
    }

    self->count = payload[COUNT_OFFSET];
    self->flags = payload[FLAGS_OFFSET];
    if (self->count > ADMIN_GROUP_LIST_REPLY_ENTRIES_MAX) {
        return false;
    }
    if (payload_length < ADMIN_GROUP_LIST_REPLY_FIXED_FIELDS_SIZE + (size_t)self->count * ADMIN_GROUP_LIST_REPLY_ENTRY_SIZE) {
        return false;
    }

    for(i=0; i<self->count; i++) {
        if (!readGroupEntry(payload + ENTRIES_OFFSET + i * ADMIN_GROUP_LIST_REPLY_ENTRY_SIZE, &self->entries[i])) {
            return false;
        }
    }

    return true;
}

/* ---------- private ---------- */

static size_t encodeFixedBody(uint8_t type, size_t body_size, uint8_t *buffer, size_t buffer_size, uint8_t **body)
{
    MessageHeader header;
    size_t total_size = MESSAGE_HEADER_SIZE + body_size;

    if (buffer_size < total_size) {
        return 0;
    }

    header.type = type;
    header.flags = 0;
    header.length = (uint16_t)body_size;
    MessageHeader_Encode(&header, buffer, buffer_size);

    *body = buffer + MESSAGE_HEADER_SIZE;
    memset(*body, 0, body_size);

    return total_size;
}

static bool isNameTerminated(const char *agent_name)
{
    return agent_name[REGISTER_AGENT_NAME_SIZE - 1] == '\0';
}

static size_t encodeAgentName(uint8_t type, const char *agent_name, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;
    size_t total_size;

    if (!isNameTerminated(agent_name) || agent_name[0] == '\0') {
        return 0;
    }

    total_size = encodeFixedBody(type, REGISTER_AGENT_NAME_SIZE, buffer, buffer_size, &body);
    if (total_size == 0) {
        return 0;
    }

    memcpy(body + AGENT_NAME_OFFSET, agent_name, strlen(agent_name));

    return total_size;
}

static bool decodeAgentName(char *agent_name, const uint8_t *payload, size_t payload_length, size_t body_size)
{
    if (payload_length < body_size) {
        return false;
    }

    memcpy(agent_name, payload + AGENT_NAME_OFFSET, REGISTER_AGENT_NAME_SIZE);
    if (agent_name[REGISTER_AGENT_NAME_SIZE - 1] != '\0' || agent_name[0] == '\0') {
        return false;
    }

    return true;
}

static size_t encodeStatusReply(uint8_t type, uint8_t status, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;
    size_t total_size;

    total_size = encodeFixedBody(type, ADMIN_KICK_REPLY_BODY_SIZE, buffer, buffer_size, &body);
    if (total_size == 0) {
        return 0;
    }

    body[STATUS_OFFSET] = status;

    return total_size;
}

static bool decodeStatusReply(uint8_t *status, const uint8_t *payload, size_t payload_length, size_t body_size)
{
    if (payload_length < body_size) {
        return false;
    }

    *status = payload[STATUS_OFFSET];

    return true;
}

static size_t encodeOffsetRequest(uint8_t type, uint16_t offset, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;
    size_t total_size;

    total_size = encodeFixedBody(type, ADMIN_PEERS_BODY_SIZE, buffer, buffer_size, &body);
    if (total_size == 0) {
        return 0;
    }

    Wire_WriteU16(body + OFFSET_OFFSET, offset);

    return total_size;
}

static size_t encodeFilteredRequest(
    uint8_t type,
    uint16_t offset,
    const char *agent_name,
    uint8_t *buffer,
    size_t buffer_size
)
{
    uint8_t *body;
    size_t total_size;

    if (!isNameTerminated(agent_name)) {
        return 0;
    }

    total_size = encodeFixedBody(type, ADMIN_AGENTS_BODY_SIZE, buffer, buffer_size, &body);
    if (total_size == 0) {
        return 0;
    }

    Wire_WriteU16(body + OFFSET_OFFSET, offset);
    memcpy(body + FILTER_NAME_OFFSET, agent_name, strlen(agent_name));

    return total_size;
}

static bool decodeFilteredRequest(
    uint16_t *offset,
    char *agent_name,
    const uint8_t *payload,
    size_t payload_length
)
{
    if (payload_length < ADMIN_AGENTS_BODY_SIZE) {
        return false;
    }

    *offset = Wire_ReadU16(payload + OFFSET_OFFSET);
    memcpy(agent_name, payload + FILTER_NAME_OFFSET, REGISTER_AGENT_NAME_SIZE);

    return agent_name[REGISTER_AGENT_NAME_SIZE - 1] == '\0';
}

static void writePeerEntry(uint8_t *destination, const AdminPeersReplyEntry *entry)
{
    Wire_WriteU32(destination + PEER_ENTRY_PEER_ID_OFFSET, entry->peer_id);
    Wire_WriteU32(destination + PEER_ENTRY_FRAMES_FORWARDED_OFFSET, entry->frames_forwarded);
    Wire_WriteU32(destination + PEER_ENTRY_FRAMES_DROPPED_OFFSET, entry->frames_dropped);
    Wire_WriteU32(destination + PEER_ENTRY_UPTIME_OFFSET, entry->uptime_seconds);
    destination[PEER_ENTRY_ROLE_OFFSET] = entry->role;
    destination[PEER_ENTRY_TRANSPORT_KIND_OFFSET] = entry->transport_kind;
    memcpy(destination + PEER_ENTRY_AGENT_NAME_OFFSET, entry->agent_name, strlen(entry->agent_name));
    memcpy(destination + PEER_ENTRY_FINGERPRINT_OFFSET, entry->fingerprint_hex, strlen(entry->fingerprint_hex));
    memcpy(destination + PEER_ENTRY_ORIGIN_OFFSET, entry->origin, strlen(entry->origin));
}

static void readPeerEntry(const uint8_t *source, AdminPeersReplyEntry *entry)
{
    entry->peer_id = Wire_ReadU32(source + PEER_ENTRY_PEER_ID_OFFSET);
    entry->frames_forwarded = Wire_ReadU32(source + PEER_ENTRY_FRAMES_FORWARDED_OFFSET);
    entry->frames_dropped = Wire_ReadU32(source + PEER_ENTRY_FRAMES_DROPPED_OFFSET);
    entry->uptime_seconds = Wire_ReadU32(source + PEER_ENTRY_UPTIME_OFFSET);
    entry->role = source[PEER_ENTRY_ROLE_OFFSET];
    entry->transport_kind = source[PEER_ENTRY_TRANSPORT_KIND_OFFSET];
    memcpy(entry->agent_name, source + PEER_ENTRY_AGENT_NAME_OFFSET, REGISTER_AGENT_NAME_SIZE);
    entry->agent_name[REGISTER_AGENT_NAME_SIZE - 1] = '\0';
    memcpy(entry->fingerprint_hex, source + PEER_ENTRY_FINGERPRINT_OFFSET, ADMIN_FINGERPRINT_HEX_SIZE);
    entry->fingerprint_hex[ADMIN_FINGERPRINT_HEX_SIZE - 1] = '\0';
    memcpy(entry->origin, source + PEER_ENTRY_ORIGIN_OFFSET, ADMIN_ORIGIN_SIZE);
    entry->origin[ADMIN_ORIGIN_SIZE - 1] = '\0';
}

static void writePinEntry(uint8_t *destination, const AdminPinsReplyEntry *entry)
{
    memcpy(destination + PIN_ENTRY_AGENT_NAME_OFFSET, entry->agent_name, strlen(entry->agent_name));
    memcpy(destination + PIN_ENTRY_FINGERPRINT_OFFSET, entry->fingerprint_hex, strlen(entry->fingerprint_hex));
}

static void readPinEntry(const uint8_t *source, AdminPinsReplyEntry *entry)
{
    memcpy(entry->agent_name, source + PIN_ENTRY_AGENT_NAME_OFFSET, REGISTER_AGENT_NAME_SIZE);
    entry->agent_name[REGISTER_AGENT_NAME_SIZE - 1] = '\0';
    memcpy(entry->fingerprint_hex, source + PIN_ENTRY_FINGERPRINT_OFFSET, ADMIN_FINGERPRINT_HEX_SIZE);
    entry->fingerprint_hex[ADMIN_FINGERPRINT_HEX_SIZE - 1] = '\0';
}

static void writeAgentEntry(uint8_t *destination, const AdminAgentsReplyEntry *entry)
{
    Wire_WriteU32(destination + AGENT_ENTRY_PEER_ID_OFFSET, entry->peer_id);
    destination[AGENT_ENTRY_INTERFACE_COUNT_OFFSET] = entry->interface_count;
    memcpy(destination + AGENT_ENTRY_AGENT_NAME_OFFSET, entry->agent_name, strlen(entry->agent_name));
    memcpy(destination + AGENT_ENTRY_FINGERPRINT_OFFSET, entry->fingerprint_hex, strlen(entry->fingerprint_hex));
}

static void readAgentEntry(const uint8_t *source, AdminAgentsReplyEntry *entry)
{
    entry->peer_id = Wire_ReadU32(source + AGENT_ENTRY_PEER_ID_OFFSET);
    entry->interface_count = source[AGENT_ENTRY_INTERFACE_COUNT_OFFSET];
    memcpy(entry->agent_name, source + AGENT_ENTRY_AGENT_NAME_OFFSET, REGISTER_AGENT_NAME_SIZE);
    entry->agent_name[REGISTER_AGENT_NAME_SIZE - 1] = '\0';
    memcpy(entry->fingerprint_hex, source + AGENT_ENTRY_FINGERPRINT_OFFSET, ADMIN_FINGERPRINT_HEX_SIZE);
    entry->fingerprint_hex[ADMIN_FINGERPRINT_HEX_SIZE - 1] = '\0';
}

static void writeClientEntry(uint8_t *destination, const AdminClientsReplyEntry *entry)
{
    Wire_WriteU32(destination + CLIENT_ENTRY_PEER_ID_OFFSET, entry->peer_id);
    Wire_WriteU32(destination + CLIENT_ENTRY_INTERFACE_ID_OFFSET, entry->interface_id);
    destination[CLIENT_ENTRY_CHANNEL_OFFSET] = entry->channel;
    memcpy(destination + CLIENT_ENTRY_AGENT_NAME_OFFSET, entry->agent_name, strlen(entry->agent_name));
    memcpy(destination + CLIENT_ENTRY_INTERFACE_NAME_OFFSET, entry->interface_name, strlen(entry->interface_name));
    Wire_WriteU32(destination + CLIENT_ENTRY_FRAMES_FORWARDED_OFFSET, entry->frames_forwarded);
    Wire_WriteU32(destination + CLIENT_ENTRY_FRAMES_DROPPED_OFFSET, entry->frames_dropped);
}

static void readClientEntry(const uint8_t *source, AdminClientsReplyEntry *entry)
{
    entry->peer_id = Wire_ReadU32(source + CLIENT_ENTRY_PEER_ID_OFFSET);
    entry->interface_id = Wire_ReadU32(source + CLIENT_ENTRY_INTERFACE_ID_OFFSET);
    entry->channel = source[CLIENT_ENTRY_CHANNEL_OFFSET];
    memcpy(entry->agent_name, source + CLIENT_ENTRY_AGENT_NAME_OFFSET, REGISTER_AGENT_NAME_SIZE);
    entry->agent_name[REGISTER_AGENT_NAME_SIZE - 1] = '\0';
    memcpy(entry->interface_name, source + CLIENT_ENTRY_INTERFACE_NAME_OFFSET, REGISTER_INTERFACE_NAME_SIZE);
    entry->interface_name[REGISTER_INTERFACE_NAME_SIZE - 1] = '\0';
    entry->frames_forwarded = Wire_ReadU32(source + CLIENT_ENTRY_FRAMES_FORWARDED_OFFSET);
    entry->frames_dropped = Wire_ReadU32(source + CLIENT_ENTRY_FRAMES_DROPPED_OFFSET);
}

static void writeAclEntry(uint8_t *destination, const AdminAclListReplyEntry *entry)
{
    memcpy(destination + ACL_AGENT_NAME_OFFSET, entry->agent_name, strlen(entry->agent_name));
    memcpy(destination + ACL_INTERFACE_NAME_OFFSET, entry->interface_name, strlen(entry->interface_name));
    memcpy(destination + ACL_FINGERPRINT_OFFSET, entry->fingerprint_hex, strlen(entry->fingerprint_hex));
    destination[ACL_CAN_READ_OFFSET] = entry->can_read;
    destination[ACL_CAN_WRITE_OFFSET] = entry->can_write;
    destination[ACL_CAN_ADMIN_OFFSET] = entry->can_admin;
}

static void readAclEntry(const uint8_t *source, AdminAclListReplyEntry *entry)
{
    memcpy(entry->agent_name, source + ACL_AGENT_NAME_OFFSET, REGISTER_AGENT_NAME_SIZE);
    entry->agent_name[REGISTER_AGENT_NAME_SIZE - 1] = '\0';
    memcpy(entry->interface_name, source + ACL_INTERFACE_NAME_OFFSET, REGISTER_INTERFACE_NAME_SIZE);
    entry->interface_name[REGISTER_INTERFACE_NAME_SIZE - 1] = '\0';
    memcpy(entry->fingerprint_hex, source + ACL_FINGERPRINT_OFFSET, ADMIN_FINGERPRINT_HEX_SIZE);
    entry->fingerprint_hex[ADMIN_FINGERPRINT_HEX_SIZE - 1] = '\0';
    entry->can_read = source[ACL_CAN_READ_OFFSET];
    entry->can_write = source[ACL_CAN_WRITE_OFFSET];
    entry->can_admin = source[ACL_CAN_ADMIN_OFFSET];
}

static void writeInterfaceEntry(uint8_t *destination, const AdminInterfacesReplyEntry *entry)
{
    Wire_WriteU32(destination + INTERFACE_ENTRY_ID_OFFSET, entry->interface_id);
    destination[INTERFACE_ENTRY_SUBSCRIBERS_OFFSET] = entry->subscriber_count;
    destination[INTERFACE_ENTRY_MODE_OFFSET] = entry->mode;
    Wire_WriteU64(destination + INTERFACE_ENTRY_FRAMES_OFFSET, entry->frames_received);
    memcpy(destination + INTERFACE_ENTRY_AGENT_NAME_OFFSET, entry->agent_name, strlen(entry->agent_name));
    memcpy(destination + INTERFACE_ENTRY_INTERFACE_NAME_OFFSET, entry->interface_name, strlen(entry->interface_name));
    Wire_WriteU64(destination + INTERFACE_ENTRY_TX_DROPPED_OFFSET, entry->tx_dropped);
}

static void readInterfaceEntry(const uint8_t *source, AdminInterfacesReplyEntry *entry)
{
    entry->interface_id = Wire_ReadU32(source + INTERFACE_ENTRY_ID_OFFSET);
    entry->subscriber_count = source[INTERFACE_ENTRY_SUBSCRIBERS_OFFSET];
    entry->mode = source[INTERFACE_ENTRY_MODE_OFFSET];
    entry->frames_received = Wire_ReadU64(source + INTERFACE_ENTRY_FRAMES_OFFSET);
    memcpy(entry->agent_name, source + INTERFACE_ENTRY_AGENT_NAME_OFFSET, REGISTER_AGENT_NAME_SIZE);
    entry->agent_name[REGISTER_AGENT_NAME_SIZE - 1] = '\0';
    memcpy(entry->interface_name, source + INTERFACE_ENTRY_INTERFACE_NAME_OFFSET, REGISTER_INTERFACE_NAME_SIZE);
    entry->interface_name[REGISTER_INTERFACE_NAME_SIZE - 1] = '\0';
    entry->tx_dropped = Wire_ReadU64(source + INTERFACE_ENTRY_TX_DROPPED_OFFSET);
}

static bool isRejectedEntryTerminated(const AdminRejectedReplyEntry *entry)
{
    if (!isNameTerminated(entry->name)) {
        return false;
    }
    if (entry->fingerprint_hex[ADMIN_FINGERPRINT_HEX_SIZE - 1] != '\0') {
        return false;
    }

    return entry->origin[ADMIN_ORIGIN_SIZE - 1] == '\0';
}

static void writeRejectedEntry(uint8_t *destination, const AdminRejectedReplyEntry *entry)
{
    destination[REJECTED_ENTRY_ROLE_OFFSET] = entry->role;
    destination[REJECTED_ENTRY_REASON_OFFSET] = entry->reason;
    Wire_WriteU32(destination + REJECTED_ENTRY_ATTEMPTS_OFFSET, entry->attempts);
    Wire_WriteU32(destination + REJECTED_ENTRY_LAST_SEEN_OFFSET, entry->last_seen_seconds);
    memcpy(destination + REJECTED_ENTRY_NAME_OFFSET, entry->name, strlen(entry->name));
    memcpy(destination + REJECTED_ENTRY_FINGERPRINT_OFFSET, entry->fingerprint_hex, strlen(entry->fingerprint_hex));
    memcpy(destination + REJECTED_ENTRY_ORIGIN_OFFSET, entry->origin, strlen(entry->origin));
}

static void readRejectedEntry(const uint8_t *source, AdminRejectedReplyEntry *entry)
{
    entry->role = source[REJECTED_ENTRY_ROLE_OFFSET];
    entry->reason = source[REJECTED_ENTRY_REASON_OFFSET];
    entry->attempts = Wire_ReadU32(source + REJECTED_ENTRY_ATTEMPTS_OFFSET);
    entry->last_seen_seconds = Wire_ReadU32(source + REJECTED_ENTRY_LAST_SEEN_OFFSET);
    memcpy(entry->name, source + REJECTED_ENTRY_NAME_OFFSET, REGISTER_AGENT_NAME_SIZE);
    entry->name[REGISTER_AGENT_NAME_SIZE - 1] = '\0';
    memcpy(entry->fingerprint_hex, source + REJECTED_ENTRY_FINGERPRINT_OFFSET, ADMIN_FINGERPRINT_HEX_SIZE);
    entry->fingerprint_hex[ADMIN_FINGERPRINT_HEX_SIZE - 1] = '\0';
    memcpy(entry->origin, source + REJECTED_ENTRY_ORIGIN_OFFSET, ADMIN_ORIGIN_SIZE);
    entry->origin[ADMIN_ORIGIN_SIZE - 1] = '\0';
}

static bool isBridgeEndpointValid(const AdminBridgeEndpoint *endpoint)
{
    if (!isNameTerminated(endpoint->agent_name) || endpoint->agent_name[0] == '\0') {
        return false;
    }

    return endpoint->interface_name[REGISTER_INTERFACE_NAME_SIZE - 1] == '\0' && endpoint->interface_name[0] != '\0';
}

static void writeBridgeEndpoint(uint8_t *destination, const AdminBridgeEndpoint *endpoint)
{
    memcpy(destination, endpoint->agent_name, strlen(endpoint->agent_name));
    memcpy(destination + BRIDGE_ENDPOINT_INTERFACE_NAME_OFFSET, endpoint->interface_name, strlen(endpoint->interface_name));
}

static bool readBridgeEndpoint(const uint8_t *source, AdminBridgeEndpoint *endpoint)
{
    memcpy(endpoint->agent_name, source, REGISTER_AGENT_NAME_SIZE);
    endpoint->agent_name[REGISTER_AGENT_NAME_SIZE - 1] = '\0';
    memcpy(endpoint->interface_name, source + BRIDGE_ENDPOINT_INTERFACE_NAME_OFFSET, REGISTER_INTERFACE_NAME_SIZE);
    endpoint->interface_name[REGISTER_INTERFACE_NAME_SIZE - 1] = '\0';

    return endpoint->agent_name[0] != '\0' && endpoint->interface_name[0] != '\0';
}

static void writeBridgeFilters(uint8_t *destination, const CanFilter *filters, uint8_t filter_count)
{
    uint8_t i;

    for(i=0; i<filter_count; i++) {
        Wire_WriteU32(destination + i * CAN_FILTER_SIZE, filters[i].can_id);
        Wire_WriteU32(destination + i * CAN_FILTER_SIZE + BRIDGE_FILTER_MASK_OFFSET, filters[i].can_mask);
    }
}

static void readBridgeFilters(const uint8_t *source, CanFilter *filters, uint8_t filter_count)
{
    uint8_t i;

    for(i=0; i<filter_count; i++) {
        filters[i].can_id = Wire_ReadU32(source + i * CAN_FILTER_SIZE);
        filters[i].can_mask = Wire_ReadU32(source + i * CAN_FILTER_SIZE + BRIDGE_FILTER_MASK_OFFSET);
    }
}

static void writeBridgeEntry(uint8_t *destination, const AdminBridgeListReplyEntry *entry)
{
    writeBridgeEndpoint(destination + BRIDGE_SOURCE_OFFSET, &entry->source);
    writeBridgeEndpoint(destination + BRIDGE_DESTINATION_OFFSET, &entry->destination);
    destination[BRIDGE_FILTER_COUNT_OFFSET] = entry->filter_count;
    destination[BRIDGE_STATE_OFFSET] = entry->state;
    writeBridgeFilters(destination + BRIDGE_FILTERS_OFFSET, entry->filters, entry->filter_count);
    Wire_WriteU64(destination + BRIDGE_ENTRY_FORWARDED_OFFSET, entry->forwarded);
    Wire_WriteU64(destination + BRIDGE_ENTRY_DROPPED_OFFSET, entry->dropped);
}

static bool readBridgeEntry(const uint8_t *source, AdminBridgeListReplyEntry *entry)
{
    if (!readBridgeEndpoint(source + BRIDGE_SOURCE_OFFSET, &entry->source)) {
        return false;
    }
    if (!readBridgeEndpoint(source + BRIDGE_DESTINATION_OFFSET, &entry->destination)) {
        return false;
    }

    entry->filter_count = source[BRIDGE_FILTER_COUNT_OFFSET];
    if (entry->filter_count > SUBSCRIBE_FILTERS_MAX) {
        return false;
    }
    entry->state = source[BRIDGE_STATE_OFFSET];
    readBridgeFilters(source + BRIDGE_FILTERS_OFFSET, entry->filters, entry->filter_count);
    entry->forwarded = Wire_ReadU64(source + BRIDGE_ENTRY_FORWARDED_OFFSET);
    entry->dropped = Wire_ReadU64(source + BRIDGE_ENTRY_DROPPED_OFFSET);

    return true;
}

static bool isGroupNameValid(const char *name)
{
    return name[ADMIN_GROUP_NAME_SIZE - 1] == '\0' && name[0] != '\0';
}

static bool readGroupName(const uint8_t *source, char *name)
{
    memcpy(name, source, ADMIN_GROUP_NAME_SIZE);
    name[ADMIN_GROUP_NAME_SIZE - 1] = '\0';

    return name[0] != '\0';
}

static bool areGroupMembersValid(const AdminGroupListReplyEntry *entry)
{
    uint8_t i;

    for(i=0; i<entry->member_count; i++) {
        if (entry->members[i].filter_count > ADMIN_GROUP_MEMBER_FILTERS_MAX) {
            return false;
        }
    }

    return true;
}

static void writeGroupEntry(uint8_t *destination, const AdminGroupListReplyEntry *entry)
{
    uint8_t *member;
    uint8_t i;

    memcpy(destination + GROUP_NAME_OFFSET, entry->name, strlen(entry->name));
    destination[GROUP_FILTER_COUNT_OFFSET] = entry->filter_count;
    destination[GROUP_ENTRY_MEMBER_COUNT_OFFSET] = entry->member_count;
    writeBridgeFilters(destination + GROUP_FILTERS_OFFSET, entry->filters, entry->filter_count);
    Wire_WriteU64(destination + GROUP_ENTRY_FORWARDED_OFFSET, entry->forwarded);
    Wire_WriteU64(destination + GROUP_ENTRY_DROPPED_OFFSET, entry->dropped);
    for(i=0; i<entry->member_count; i++) {
        member = destination + GROUP_ENTRY_MEMBERS_OFFSET + i * GROUP_ENTRY_MEMBER_SIZE;
        writeBridgeEndpoint(member, &entry->members[i].endpoint);
        member[GROUP_ENTRY_MEMBER_STATE_OFFSET] = entry->members[i].state;
        member[GROUP_ENTRY_MEMBER_DIRECTION_OFFSET] = entry->members[i].direction;
        member[GROUP_ENTRY_MEMBER_FILTER_COUNT_OFFSET] = entry->members[i].filter_count;
        writeBridgeFilters(member + GROUP_ENTRY_MEMBER_FILTERS_OFFSET, entry->members[i].filters, entry->members[i].filter_count);
    }
}

static bool readGroupEntry(const uint8_t *source, AdminGroupListReplyEntry *entry)
{
    const uint8_t *member;
    uint8_t i;

    if (!readGroupName(source + GROUP_NAME_OFFSET, entry->name)) {
        return false;
    }

    entry->filter_count = source[GROUP_FILTER_COUNT_OFFSET];
    entry->member_count = source[GROUP_ENTRY_MEMBER_COUNT_OFFSET];
    if (entry->filter_count > SUBSCRIBE_FILTERS_MAX || entry->member_count > ADMIN_GROUP_MEMBERS_MAX) {
        return false;
    }
    readBridgeFilters(source + GROUP_FILTERS_OFFSET, entry->filters, entry->filter_count);
    entry->forwarded = Wire_ReadU64(source + GROUP_ENTRY_FORWARDED_OFFSET);
    entry->dropped = Wire_ReadU64(source + GROUP_ENTRY_DROPPED_OFFSET);
    for(i=0; i<entry->member_count; i++) {
        member = source + GROUP_ENTRY_MEMBERS_OFFSET + i * GROUP_ENTRY_MEMBER_SIZE;
        if (!readBridgeEndpoint(member, &entry->members[i].endpoint)) {
            return false;
        }
        entry->members[i].state = member[GROUP_ENTRY_MEMBER_STATE_OFFSET];
        entry->members[i].direction = member[GROUP_ENTRY_MEMBER_DIRECTION_OFFSET];
        entry->members[i].filter_count = member[GROUP_ENTRY_MEMBER_FILTER_COUNT_OFFSET];
        if (entry->members[i].filter_count > ADMIN_GROUP_MEMBER_FILTERS_MAX) {
            return false;
        }
        readBridgeFilters(member + GROUP_ENTRY_MEMBER_FILTERS_OFFSET, entry->members[i].filters, entry->members[i].filter_count);
    }

    return true;
}
