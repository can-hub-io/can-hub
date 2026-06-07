#include "protocol/admin_message.h"

#include <string.h>

#include "protocol/message_header.h"
#include "protocol/wire.h"

#define PEER_COUNT_OFFSET 0
#define AGENT_COUNT_OFFSET 2
#define CLIENT_COUNT_OFFSET 4
#define INTERFACE_COUNT_OFFSET 6
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
#define PEER_ENTRY_AGENT_NAME_OFFSET 16
#define PEER_ENTRY_FINGERPRINT_OFFSET 144

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

#define INTERFACE_ENTRY_ID_OFFSET 0
#define INTERFACE_ENTRY_SUBSCRIBERS_OFFSET 4
#define INTERFACE_ENTRY_FRAMES_OFFSET 8
#define INTERFACE_ENTRY_AGENT_NAME_OFFSET 16
#define INTERFACE_ENTRY_INTERFACE_NAME_OFFSET 144

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
static void writeInterfaceEntry(uint8_t *destination, const AdminInterfacesReplyEntry *entry);
static void readInterfaceEntry(const uint8_t *source, AdminInterfacesReplyEntry *entry);

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
    destination[PEER_ENTRY_ROLE_OFFSET] = entry->role;
    memcpy(destination + PEER_ENTRY_AGENT_NAME_OFFSET, entry->agent_name, strlen(entry->agent_name));
    memcpy(destination + PEER_ENTRY_FINGERPRINT_OFFSET, entry->fingerprint_hex, strlen(entry->fingerprint_hex));
}

static void readPeerEntry(const uint8_t *source, AdminPeersReplyEntry *entry)
{
    entry->peer_id = Wire_ReadU32(source + PEER_ENTRY_PEER_ID_OFFSET);
    entry->frames_forwarded = Wire_ReadU32(source + PEER_ENTRY_FRAMES_FORWARDED_OFFSET);
    entry->frames_dropped = Wire_ReadU32(source + PEER_ENTRY_FRAMES_DROPPED_OFFSET);
    entry->role = source[PEER_ENTRY_ROLE_OFFSET];
    memcpy(entry->agent_name, source + PEER_ENTRY_AGENT_NAME_OFFSET, REGISTER_AGENT_NAME_SIZE);
    entry->agent_name[REGISTER_AGENT_NAME_SIZE - 1] = '\0';
    memcpy(entry->fingerprint_hex, source + PEER_ENTRY_FINGERPRINT_OFFSET, ADMIN_FINGERPRINT_HEX_SIZE);
    entry->fingerprint_hex[ADMIN_FINGERPRINT_HEX_SIZE - 1] = '\0';
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
}

static void writeInterfaceEntry(uint8_t *destination, const AdminInterfacesReplyEntry *entry)
{
    Wire_WriteU32(destination + INTERFACE_ENTRY_ID_OFFSET, entry->interface_id);
    destination[INTERFACE_ENTRY_SUBSCRIBERS_OFFSET] = entry->subscriber_count;
    Wire_WriteU64(destination + INTERFACE_ENTRY_FRAMES_OFFSET, entry->frames_received);
    memcpy(destination + INTERFACE_ENTRY_AGENT_NAME_OFFSET, entry->agent_name, strlen(entry->agent_name));
    memcpy(destination + INTERFACE_ENTRY_INTERFACE_NAME_OFFSET, entry->interface_name, strlen(entry->interface_name));
}

static void readInterfaceEntry(const uint8_t *source, AdminInterfacesReplyEntry *entry)
{
    entry->interface_id = Wire_ReadU32(source + INTERFACE_ENTRY_ID_OFFSET);
    entry->subscriber_count = source[INTERFACE_ENTRY_SUBSCRIBERS_OFFSET];
    entry->frames_received = Wire_ReadU64(source + INTERFACE_ENTRY_FRAMES_OFFSET);
    memcpy(entry->agent_name, source + INTERFACE_ENTRY_AGENT_NAME_OFFSET, REGISTER_AGENT_NAME_SIZE);
    entry->agent_name[REGISTER_AGENT_NAME_SIZE - 1] = '\0';
    memcpy(entry->interface_name, source + INTERFACE_ENTRY_INTERFACE_NAME_OFFSET, REGISTER_INTERFACE_NAME_SIZE);
    entry->interface_name[REGISTER_INTERFACE_NAME_SIZE - 1] = '\0';
}
