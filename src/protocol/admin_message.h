#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol/register_message.h"
#include "protocol/subscribe_message.h"

#define ADMIN_STATUS_REPLY_BODY_SIZE 44
#define ADMIN_PEERS_BODY_SIZE 4
#define ADMIN_PEERS_REPLY_FIXED_FIELDS_SIZE 4
#define ADMIN_PEERS_REPLY_ENTRIES_MAX 15
#define ADMIN_PEERS_REPLY_ENTRY_SIZE 272
#define ADMIN_ORIGIN_SIZE 56
#define ADMIN_KICK_BODY_SIZE 128
#define ADMIN_KICK_REPLY_BODY_SIZE 4
#define ADMIN_PINS_BODY_SIZE 4
#define ADMIN_PINS_REPLY_FIXED_FIELDS_SIZE 4
#define ADMIN_PINS_REPLY_ENTRIES_MAX 16
#define ADMIN_PINS_REPLY_ENTRY_SIZE 196
#define ADMIN_FORGET_BODY_SIZE 128
#define ADMIN_FORGET_REPLY_BODY_SIZE 4
#define ADMIN_PIN_ADD_BODY_SIZE 196
#define ADMIN_PIN_ADD_REPLY_BODY_SIZE 4
#define ADMIN_ACL_SET_BODY_SIZE 212
#define ADMIN_ACL_SET_REPLY_BODY_SIZE 4
#define ADMIN_ACL_REVOKE_BODY_SIZE 212
#define ADMIN_ACL_REVOKE_REPLY_BODY_SIZE 4
#define ADMIN_ACL_LIST_BODY_SIZE 4
#define ADMIN_ACL_LIST_REPLY_FIXED_FIELDS_SIZE 4
#define ADMIN_ACL_LIST_REPLY_ENTRIES_MAX 16
#define ADMIN_ACL_LIST_REPLY_ENTRY_SIZE 212
#define ADMIN_KICK_PEER_BODY_SIZE 4
#define ADMIN_KICK_PEER_REPLY_BODY_SIZE 4
#define ADMIN_AGENTS_BODY_SIZE 132
#define ADMIN_AGENTS_REPLY_FIXED_FIELDS_SIZE 4
#define ADMIN_AGENTS_REPLY_ENTRIES_MAX 16
#define ADMIN_AGENTS_REPLY_ENTRY_SIZE 204
#define ADMIN_CLIENTS_BODY_SIZE 132
#define ADMIN_CLIENTS_REPLY_FIXED_FIELDS_SIZE 4
#define ADMIN_CLIENTS_REPLY_ENTRIES_MAX 16
#define ADMIN_CLIENTS_REPLY_ENTRY_SIZE 164
#define ADMIN_INTERFACES_BODY_SIZE 4
#define ADMIN_INTERFACES_REPLY_FIXED_FIELDS_SIZE 4
#define ADMIN_INTERFACES_REPLY_ENTRIES_MAX 16
#define ADMIN_INTERFACES_REPLY_ENTRY_SIZE 168
#define ADMIN_REJECTED_BODY_SIZE 4
#define ADMIN_REJECTED_REPLY_FIXED_FIELDS_SIZE 4
#define ADMIN_REJECTED_REPLY_ENTRIES_MAX 15
#define ADMIN_REJECTED_REPLY_ENTRY_SIZE 264
#define ADMIN_BRIDGE_ENDPOINT_SIZE 144
#define ADMIN_BRIDGE_SET_BODY_SIZE 420
#define ADMIN_BRIDGE_SET_REPLY_BODY_SIZE 4
#define ADMIN_BRIDGE_DELETE_BODY_SIZE 288
#define ADMIN_BRIDGE_DELETE_REPLY_BODY_SIZE 4
#define ADMIN_BRIDGE_LIST_BODY_SIZE 4
#define ADMIN_BRIDGE_LIST_REPLY_FIXED_FIELDS_SIZE 4
#define ADMIN_BRIDGE_LIST_REPLY_ENTRIES_MAX 8
#define ADMIN_BRIDGE_LIST_REPLY_ENTRY_SIZE 440
#define ADMIN_GROUP_NAME_SIZE 32
#define ADMIN_GROUP_MEMBERS_MAX 16
#define ADMIN_GROUP_SET_BODY_SIZE 164
#define ADMIN_GROUP_DELETE_BODY_SIZE 32
#define ADMIN_GROUP_MEMBER_BODY_SIZE 176
#define ADMIN_GROUP_REPLY_BODY_SIZE 4
#define ADMIN_GROUP_LIST_BODY_SIZE 4
#define ADMIN_GROUP_LIST_REPLY_FIXED_FIELDS_SIZE 4
#define ADMIN_GROUP_LIST_REPLY_ENTRIES_MAX 1
#define ADMIN_GROUP_LIST_REPLY_ENTRY_SIZE 2616

#define ADMIN_FINGERPRINT_HEX_SIZE 65
#define ADMIN_REPLY_FLAG_MORE (1u << 0)
#define ADMIN_CLIENT_NO_CHANNEL 0xFF

#define ADMIN_STATUS_OK 0
#define ADMIN_STATUS_UNKNOWN_AGENT 1
#define ADMIN_STATUS_UNKNOWN_PEER 1
#define ADMIN_STATUS_PIN_FAILED 1

#define ADMIN_BRIDGE_STATUS_OK 0
#define ADMIN_BRIDGE_STATUS_INVALID 1
#define ADMIN_BRIDGE_STATUS_FULL 2
#define ADMIN_BRIDGE_STATUS_STORE_FAILED 3
#define ADMIN_BRIDGE_STATUS_UNKNOWN 4

#define ADMIN_BRIDGE_STATE_SOURCE_ONLINE (1u << 0)
#define ADMIN_BRIDGE_STATE_DESTINATION_ONLINE (1u << 1)
#define ADMIN_GROUP_MEMBER_ONLINE (1u << 0)

#define ADMIN_REJECTED_REASON_NOT_ALLOWED 0
#define ADMIN_REJECTED_REASON_FINGERPRINT_MISMATCH 1

typedef struct {
    uint16_t peer_count;
    uint16_t agent_count;
    uint16_t client_count;
    uint16_t interface_count;
    uint32_t receive_overflows;
    uint64_t frames_received;
    uint64_t frames_forwarded;
    uint64_t frames_dropped;
    uint64_t frames_unroutable;
} AdminStatusReplyMessage;

typedef struct {
    uint16_t offset;
} AdminPeersMessage;

typedef struct {
    uint32_t peer_id;
    uint32_t frames_forwarded;
    uint32_t frames_dropped;
    uint32_t uptime_seconds;
    uint8_t role;
    uint8_t transport_kind;
    char agent_name[REGISTER_AGENT_NAME_SIZE];
    char fingerprint_hex[ADMIN_FINGERPRINT_HEX_SIZE];
    char origin[ADMIN_ORIGIN_SIZE];
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

typedef struct {
    uint32_t peer_id;
} AdminKickPeerMessage;

typedef struct {
    uint8_t status;
} AdminKickPeerReplyMessage;

typedef struct {
    uint16_t offset;
    char agent_name[REGISTER_AGENT_NAME_SIZE];
} AdminAgentsMessage;

typedef struct {
    uint32_t peer_id;
    uint8_t interface_count;
    char agent_name[REGISTER_AGENT_NAME_SIZE];
    char fingerprint_hex[ADMIN_FINGERPRINT_HEX_SIZE];
} AdminAgentsReplyEntry;

typedef struct {
    uint8_t count;
    uint8_t flags;
    AdminAgentsReplyEntry entries[ADMIN_AGENTS_REPLY_ENTRIES_MAX];
} AdminAgentsReplyMessage;

typedef struct {
    uint16_t offset;
    char agent_name[REGISTER_AGENT_NAME_SIZE];
} AdminClientsMessage;

typedef struct {
    uint32_t peer_id;
    uint32_t interface_id;
    uint8_t channel;
    char agent_name[REGISTER_AGENT_NAME_SIZE];
    char interface_name[REGISTER_INTERFACE_NAME_SIZE];
    uint32_t frames_forwarded;
    uint32_t frames_dropped;
} AdminClientsReplyEntry;

typedef struct {
    uint8_t count;
    uint8_t flags;
    AdminClientsReplyEntry entries[ADMIN_CLIENTS_REPLY_ENTRIES_MAX];
} AdminClientsReplyMessage;

typedef struct {
    char agent_name[REGISTER_AGENT_NAME_SIZE];
    char fingerprint_hex[ADMIN_FINGERPRINT_HEX_SIZE];
} AdminPinAddMessage;

typedef struct {
    uint8_t status;
} AdminPinAddReplyMessage;

typedef struct {
    char agent_name[REGISTER_AGENT_NAME_SIZE];
    char interface_name[REGISTER_INTERFACE_NAME_SIZE];
    char fingerprint_hex[ADMIN_FINGERPRINT_HEX_SIZE];
    uint8_t can_read;
    uint8_t can_write;
    uint8_t can_admin;
} AdminAclSetMessage;

typedef struct {
    uint8_t status;
} AdminAclSetReplyMessage;

typedef struct {
    char agent_name[REGISTER_AGENT_NAME_SIZE];
    char interface_name[REGISTER_INTERFACE_NAME_SIZE];
    char fingerprint_hex[ADMIN_FINGERPRINT_HEX_SIZE];
} AdminAclRevokeMessage;

typedef struct {
    uint8_t status;
} AdminAclRevokeReplyMessage;

typedef struct {
    uint16_t offset;
} AdminAclListMessage;

typedef struct {
    char agent_name[REGISTER_AGENT_NAME_SIZE];
    char interface_name[REGISTER_INTERFACE_NAME_SIZE];
    char fingerprint_hex[ADMIN_FINGERPRINT_HEX_SIZE];
    uint8_t can_read;
    uint8_t can_write;
    uint8_t can_admin;
} AdminAclListReplyEntry;

typedef struct {
    uint8_t count;
    uint8_t flags;
    AdminAclListReplyEntry entries[ADMIN_ACL_LIST_REPLY_ENTRIES_MAX];
} AdminAclListReplyMessage;

typedef struct {
    uint16_t offset;
} AdminInterfacesMessage;

typedef struct {
    uint32_t interface_id;
    uint8_t subscriber_count;
    uint64_t frames_received;
    uint64_t tx_dropped;
    char agent_name[REGISTER_AGENT_NAME_SIZE];
    char interface_name[REGISTER_INTERFACE_NAME_SIZE];
} AdminInterfacesReplyEntry;

typedef struct {
    uint8_t count;
    uint8_t flags;
    AdminInterfacesReplyEntry entries[ADMIN_INTERFACES_REPLY_ENTRIES_MAX];
} AdminInterfacesReplyMessage;

typedef struct {
    uint16_t offset;
} AdminRejectedMessage;

typedef struct {
    uint8_t role;
    uint8_t reason;
    uint32_t attempts;
    uint32_t last_seen_seconds;
    char name[REGISTER_AGENT_NAME_SIZE];
    char fingerprint_hex[ADMIN_FINGERPRINT_HEX_SIZE];
    char origin[ADMIN_ORIGIN_SIZE];
} AdminRejectedReplyEntry;

typedef struct {
    uint8_t count;
    uint8_t flags;
    AdminRejectedReplyEntry entries[ADMIN_REJECTED_REPLY_ENTRIES_MAX];
} AdminRejectedReplyMessage;

typedef struct {
    char agent_name[REGISTER_AGENT_NAME_SIZE];
    char interface_name[REGISTER_INTERFACE_NAME_SIZE];
} AdminBridgeEndpoint;

typedef struct {
    AdminBridgeEndpoint source;
    AdminBridgeEndpoint destination;
    uint8_t filter_count;
    CanFilter filters[SUBSCRIBE_FILTERS_MAX];
} AdminBridgeSetMessage;

typedef struct {
    uint8_t status;
} AdminBridgeSetReplyMessage;

typedef struct {
    AdminBridgeEndpoint source;
    AdminBridgeEndpoint destination;
} AdminBridgeDeleteMessage;

typedef struct {
    uint8_t status;
} AdminBridgeDeleteReplyMessage;

typedef struct {
    uint16_t offset;
} AdminBridgeListMessage;

typedef struct {
    AdminBridgeEndpoint source;
    AdminBridgeEndpoint destination;
    uint8_t filter_count;
    uint8_t state;
    CanFilter filters[SUBSCRIBE_FILTERS_MAX];
    uint64_t forwarded;
    uint64_t dropped;
} AdminBridgeListReplyEntry;

typedef struct {
    uint8_t count;
    uint8_t flags;
    AdminBridgeListReplyEntry entries[ADMIN_BRIDGE_LIST_REPLY_ENTRIES_MAX];
} AdminBridgeListReplyMessage;

typedef struct {
    char name[ADMIN_GROUP_NAME_SIZE];
    uint8_t filter_count;
    CanFilter filters[SUBSCRIBE_FILTERS_MAX];
} AdminGroupSetMessage;

typedef struct {
    char name[ADMIN_GROUP_NAME_SIZE];
} AdminGroupDeleteMessage;

typedef struct {
    char name[ADMIN_GROUP_NAME_SIZE];
    AdminBridgeEndpoint member;
} AdminGroupMemberMessage;

typedef struct {
    uint8_t status;
} AdminGroupReplyMessage;

typedef struct {
    uint16_t offset;
} AdminGroupListMessage;

typedef struct {
    AdminBridgeEndpoint endpoint;
    uint8_t state;
} AdminGroupMember;

typedef struct {
    char name[ADMIN_GROUP_NAME_SIZE];
    uint8_t filter_count;
    uint8_t member_count;
    CanFilter filters[SUBSCRIBE_FILTERS_MAX];
    uint64_t forwarded;
    uint64_t dropped;
    AdminGroupMember members[ADMIN_GROUP_MEMBERS_MAX];
} AdminGroupListReplyEntry;

typedef struct {
    uint8_t count;
    uint8_t flags;
    AdminGroupListReplyEntry entries[ADMIN_GROUP_LIST_REPLY_ENTRIES_MAX];
} AdminGroupListReplyMessage;

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

size_t AdminKickPeerMessage_Encode(const AdminKickPeerMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminKickPeerMessage_Decode(AdminKickPeerMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminKickPeerReplyMessage_Encode(const AdminKickPeerReplyMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminKickPeerReplyMessage_Decode(AdminKickPeerReplyMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminAgentsMessage_Encode(const AdminAgentsMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminAgentsMessage_Decode(AdminAgentsMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminAgentsReplyMessage_Encode(const AdminAgentsReplyMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminAgentsReplyMessage_Decode(AdminAgentsReplyMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminClientsMessage_Encode(const AdminClientsMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminClientsMessage_Decode(AdminClientsMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminClientsReplyMessage_Encode(const AdminClientsReplyMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminClientsReplyMessage_Decode(AdminClientsReplyMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminPinAddMessage_Encode(const AdminPinAddMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminPinAddMessage_Decode(AdminPinAddMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminPinAddReplyMessage_Encode(const AdminPinAddReplyMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminPinAddReplyMessage_Decode(AdminPinAddReplyMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminAclSetMessage_Encode(const AdminAclSetMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminAclSetMessage_Decode(AdminAclSetMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminAclSetReplyMessage_Encode(const AdminAclSetReplyMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminAclSetReplyMessage_Decode(AdminAclSetReplyMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminAclRevokeMessage_Encode(const AdminAclRevokeMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminAclRevokeMessage_Decode(AdminAclRevokeMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminAclRevokeReplyMessage_Encode(const AdminAclRevokeReplyMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminAclRevokeReplyMessage_Decode(AdminAclRevokeReplyMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminAclListMessage_Encode(const AdminAclListMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminAclListMessage_Decode(AdminAclListMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminAclListReplyMessage_Encode(const AdminAclListReplyMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminAclListReplyMessage_Decode(AdminAclListReplyMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminInterfacesMessage_Encode(const AdminInterfacesMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminInterfacesMessage_Decode(AdminInterfacesMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminInterfacesReplyMessage_Encode(const AdminInterfacesReplyMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminInterfacesReplyMessage_Decode(AdminInterfacesReplyMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminRejectedMessage_Encode(const AdminRejectedMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminRejectedMessage_Decode(AdminRejectedMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminRejectedReplyMessage_Encode(const AdminRejectedReplyMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminRejectedReplyMessage_Decode(AdminRejectedReplyMessage *self, const uint8_t *payload, size_t payload_length);

const char *AdminBridgeStatus_Name(uint8_t status);

size_t AdminBridgeSetMessage_Encode(const AdminBridgeSetMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminBridgeSetMessage_Decode(AdminBridgeSetMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminBridgeSetReplyMessage_Encode(const AdminBridgeSetReplyMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminBridgeSetReplyMessage_Decode(AdminBridgeSetReplyMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminBridgeDeleteMessage_Encode(const AdminBridgeDeleteMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminBridgeDeleteMessage_Decode(AdminBridgeDeleteMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminBridgeDeleteReplyMessage_Encode(const AdminBridgeDeleteReplyMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminBridgeDeleteReplyMessage_Decode(AdminBridgeDeleteReplyMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminBridgeListMessage_Encode(const AdminBridgeListMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminBridgeListMessage_Decode(AdminBridgeListMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminBridgeListReplyMessage_Encode(const AdminBridgeListReplyMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminBridgeListReplyMessage_Decode(AdminBridgeListReplyMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminGroupSetMessage_Encode(const AdminGroupSetMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminGroupSetMessage_Decode(AdminGroupSetMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminGroupDeleteMessage_Encode(const AdminGroupDeleteMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminGroupDeleteMessage_Decode(AdminGroupDeleteMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminGroupMemberMessage_Encode(uint8_t type, const AdminGroupMemberMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminGroupMemberMessage_Decode(AdminGroupMemberMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminGroupReplyMessage_Encode(uint8_t type, const AdminGroupReplyMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminGroupReplyMessage_Decode(AdminGroupReplyMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminGroupListMessage_Encode(const AdminGroupListMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminGroupListMessage_Decode(AdminGroupListMessage *self, const uint8_t *payload, size_t payload_length);

size_t AdminGroupListReplyMessage_Encode(const AdminGroupListReplyMessage *self, uint8_t *buffer, size_t buffer_size);
bool AdminGroupListReplyMessage_Decode(AdminGroupListReplyMessage *self, const uint8_t *payload, size_t payload_length);
