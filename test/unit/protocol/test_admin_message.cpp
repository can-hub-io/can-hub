#include <cest>

#include <cstring>

extern "C" {
#include "protocol/admin_message.h"
#include "protocol/message_header.h"
#include "hub/ports/hub_transport_events.h"
}

#define FINGERPRINT "aa11bb22cc33dd44ee55ff66aa77bb88cc99dd00ee11ff22aa33bb44cc55dd66"

describe("admin_status_message", []() {
    it("encodes an empty status request", []() {
        uint8_t buffer[16];

        MessageHeader header;
        size_t encoded_size;

        encoded_size = AdminStatusMessage_Encode(buffer, sizeof(buffer));
        MessageHeader_Decode(&header, buffer, encoded_size);

        expect(encoded_size).toBe((size_t)MESSAGE_HEADER_SIZE);
        expect(header.type).toBe(kMESSAGE_TYPE_ADMIN_STATUS);
        expect(header.length).toBe(0);
    });

    it("round-trips the status reply counters", []() {
        AdminStatusReplyMessage reply = { 5, 2, 3, 8, 42, 5000000000ULL, 4999000000ULL, 999999, 1 };
        AdminStatusReplyMessage decoded;
        uint8_t buffer[64];
        size_t expected_size = MESSAGE_HEADER_SIZE + ADMIN_STATUS_REPLY_BODY_SIZE;
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = AdminStatusReplyMessage_Encode(&reply, buffer, sizeof(buffer));
        decoded_ok = AdminStatusReplyMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(encoded_size).toBe(expected_size);
        expect(decoded_ok).toBe(true);
        expect(decoded.peer_count).toBe(5);
        expect(decoded.agent_count).toBe(2);
        expect(decoded.client_count).toBe(3);
        expect(decoded.interface_count).toBe(8);
        expect(decoded.receive_overflows).toBe((uint32_t)42);
        expect(decoded.frames_received).toBe(5000000000ULL);
        expect(decoded.frames_forwarded).toBe(4999000000ULL);
        expect(decoded.frames_dropped).toBe((uint64_t)999999);
        expect(decoded.frames_unroutable).toBe((uint64_t)1);
    });

    it("rejects a truncated status reply", []() {
        AdminStatusReplyMessage decoded;
        uint8_t payload[ADMIN_STATUS_REPLY_BODY_SIZE] = { 0 };

        expect(AdminStatusReplyMessage_Decode(&decoded, payload, ADMIN_STATUS_REPLY_BODY_SIZE - 1)).toBe(false);
    });
});

describe("admin_peers_message", []() {
    it("round-trips the pagination offset", []() {
        AdminPeersMessage request = { 17 };
        AdminPeersMessage decoded;
        uint8_t buffer[16];
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = AdminPeersMessage_Encode(&request, buffer, sizeof(buffer));
        decoded_ok = AdminPeersMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(encoded_size).toBe((size_t)(MESSAGE_HEADER_SIZE + ADMIN_PEERS_BODY_SIZE));
        expect(decoded_ok).toBe(true);
        expect(decoded.offset).toBe(17);
    });

    it("round-trips peer entries with the more flag", []() {
        AdminPeersReplyMessage reply = { 2, ADMIN_REPLY_FLAG_MORE, {
            { 0x80000001, 1500, 7, 90, 1, kPEER_TRANSPORT_QUIC, "truck42", FINGERPRINT, "203.0.113.7:51000" },
            { 0x40000001, 0, 0, 0, 3, kPEER_TRANSPORT_UNIX, "", "", "" },
        } };
        AdminPeersReplyMessage decoded;
        uint8_t buffer[1024];
        size_t expected_size = MESSAGE_HEADER_SIZE + ADMIN_PEERS_REPLY_FIXED_FIELDS_SIZE + 2 * ADMIN_PEERS_REPLY_ENTRY_SIZE;
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = AdminPeersReplyMessage_Encode(&reply, buffer, sizeof(buffer));
        decoded_ok = AdminPeersReplyMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(encoded_size).toBe(expected_size);
        expect(decoded_ok).toBe(true);
        expect(decoded.count).toBe(2);
        expect(decoded.flags).toBe(ADMIN_REPLY_FLAG_MORE);
        expect(decoded.entries[0].peer_id).toBe((uint32_t)0x80000001);
        expect(decoded.entries[0].frames_forwarded).toBe((uint32_t)1500);
        expect(decoded.entries[0].frames_dropped).toBe((uint32_t)7);
        expect(decoded.entries[0].uptime_seconds).toBe((uint32_t)90);
        expect(decoded.entries[0].role).toBe(1);
        expect(decoded.entries[0].transport_kind).toBe((uint8_t)kPEER_TRANSPORT_QUIC);
        expect((const char *)decoded.entries[0].agent_name).toBe("truck42");
        expect((const char *)decoded.entries[0].fingerprint_hex).toBe(FINGERPRINT);
        expect((const char *)decoded.entries[0].origin).toBe("203.0.113.7:51000");
        expect(decoded.entries[1].role).toBe(3);
        expect(decoded.entries[1].transport_kind).toBe((uint8_t)kPEER_TRANSPORT_UNIX);
        expect((const char *)decoded.entries[1].agent_name).toBe("");
    });

    it("rejects more entries than the maximum", []() {
        AdminPeersReplyMessage reply = { ADMIN_PEERS_REPLY_ENTRIES_MAX + 1, 0, { } };
        uint8_t buffer[4096];

        expect(AdminPeersReplyMessage_Encode(&reply, buffer, sizeof(buffer))).toBe((size_t)0);
    });

    it("rejects a payload shorter than its declared entries", []() {
        AdminPeersReplyMessage decoded;
        uint8_t payload[ADMIN_PEERS_REPLY_FIXED_FIELDS_SIZE + ADMIN_PEERS_REPLY_ENTRY_SIZE] = { 0 };

        payload[0] = 2;

        expect(AdminPeersReplyMessage_Decode(&decoded, payload, sizeof(payload))).toBe(false);
    });
});

describe("admin_kick_message", []() {
    it("round-trips the agent name", []() {
        AdminKickMessage kick = { "truck42" };
        AdminKickMessage decoded;
        uint8_t buffer[256];
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = AdminKickMessage_Encode(&kick, buffer, sizeof(buffer));
        decoded_ok = AdminKickMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(encoded_size).toBe((size_t)(MESSAGE_HEADER_SIZE + ADMIN_KICK_BODY_SIZE));
        expect(decoded_ok).toBe(true);
        expect((const char *)decoded.agent_name).toBe("truck42");
    });

    it("rejects encoding an empty agent name", []() {
        AdminKickMessage kick = { "" };
        uint8_t buffer[256];

        expect(AdminKickMessage_Encode(&kick, buffer, sizeof(buffer))).toBe((size_t)0);
    });

    it("rejects decoding an unterminated agent name", []() {
        AdminKickMessage decoded;
        uint8_t payload[ADMIN_KICK_BODY_SIZE];

        memset(payload, 'a', sizeof(payload));

        expect(AdminKickMessage_Decode(&decoded, payload, sizeof(payload))).toBe(false);
    });

    it("round-trips the kick reply status", []() {
        AdminKickReplyMessage reply = { ADMIN_STATUS_UNKNOWN_AGENT };
        AdminKickReplyMessage decoded;
        uint8_t buffer[16];
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = AdminKickReplyMessage_Encode(&reply, buffer, sizeof(buffer));
        decoded_ok = AdminKickReplyMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(encoded_size).toBe((size_t)(MESSAGE_HEADER_SIZE + ADMIN_KICK_REPLY_BODY_SIZE));
        expect(decoded_ok).toBe(true);
        expect(decoded.status).toBe(ADMIN_STATUS_UNKNOWN_AGENT);
    });
});

describe("admin_pins_message", []() {
    it("round-trips pin entries", []() {
        AdminPinsReplyMessage reply = { 1, 0, { { "truck42", FINGERPRINT } } };
        AdminPinsReplyMessage decoded;
        uint8_t buffer[512];
        size_t expected_size = MESSAGE_HEADER_SIZE + ADMIN_PINS_REPLY_FIXED_FIELDS_SIZE + ADMIN_PINS_REPLY_ENTRY_SIZE;
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = AdminPinsReplyMessage_Encode(&reply, buffer, sizeof(buffer));
        decoded_ok = AdminPinsReplyMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(encoded_size).toBe(expected_size);
        expect(decoded_ok).toBe(true);
        expect(decoded.count).toBe(1);
        expect((const char *)decoded.entries[0].agent_name).toBe("truck42");
        expect((const char *)decoded.entries[0].fingerprint_hex).toBe(FINGERPRINT);
    });

    it("round-trips the forget request", []() {
        AdminForgetMessage message = { "truck42" };
        AdminForgetMessage decoded;
        uint8_t buffer[256];
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = AdminForgetMessage_Encode(&message, buffer, sizeof(buffer));
        decoded_ok = AdminForgetMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(decoded_ok).toBe(true);
        expect((const char *)decoded.agent_name).toBe("truck42");
    });

    it("round-trips the forget reply", []() {
        AdminForgetReplyMessage message = { ADMIN_STATUS_OK };
        AdminForgetReplyMessage decoded;
        uint8_t buffer[256];
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = AdminForgetReplyMessage_Encode(&message, buffer, sizeof(buffer));
        decoded_ok = AdminForgetReplyMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(decoded_ok).toBe(true);
        expect(decoded.status).toBe(ADMIN_STATUS_OK);
    });
});

describe("admin_kick_peer_message", []() {
    it("round-trips the peer id", []() {
        AdminKickPeerMessage message = { 0x80000001 };
        AdminKickPeerMessage decoded;
        uint8_t buffer[32];
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = AdminKickPeerMessage_Encode(&message, buffer, sizeof(buffer));
        decoded_ok = AdminKickPeerMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(encoded_size).toBe((size_t)(MESSAGE_HEADER_SIZE + ADMIN_KICK_PEER_BODY_SIZE));
        expect(decoded_ok).toBe(true);
        expect(decoded.peer_id).toBe((uint32_t)0x80000001);
    });

    it("round-trips the kick-peer reply status", []() {
        AdminKickPeerReplyMessage message = { ADMIN_STATUS_UNKNOWN_PEER };
        AdminKickPeerReplyMessage decoded;
        uint8_t buffer[256];
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = AdminKickPeerReplyMessage_Encode(&message, buffer, sizeof(buffer));
        decoded_ok = AdminKickPeerReplyMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(decoded_ok).toBe(true);
        expect(decoded.status).toBe(ADMIN_STATUS_UNKNOWN_PEER);
    });

    it("rejects a truncated kick peer payload", []() {
        AdminKickPeerMessage decoded;
        uint8_t payload[ADMIN_KICK_PEER_BODY_SIZE] = { 0 };

        expect(AdminKickPeerMessage_Decode(&decoded, payload, ADMIN_KICK_PEER_BODY_SIZE - 1)).toBe(false);
    });
});

describe("admin_agents_message", []() {
    it("round-trips the offset and the name filter", []() {
        AdminAgentsMessage request = { 5, "truck42" };
        AdminAgentsMessage decoded;
        uint8_t buffer[256];
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = AdminAgentsMessage_Encode(&request, buffer, sizeof(buffer));
        decoded_ok = AdminAgentsMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(encoded_size).toBe((size_t)(MESSAGE_HEADER_SIZE + ADMIN_AGENTS_BODY_SIZE));
        expect(decoded_ok).toBe(true);
        expect(decoded.offset).toBe(5);
        expect((const char *)decoded.agent_name).toBe("truck42");
    });

    it("accepts an empty filter", []() {
        AdminAgentsMessage request = { 0, "" };
        AdminAgentsMessage decoded;
        uint8_t buffer[256];
        size_t encoded_size;

        encoded_size = AdminAgentsMessage_Encode(&request, buffer, sizeof(buffer));

        expect(AdminAgentsMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE)).toBe(true);
        expect((const char *)decoded.agent_name).toBe("");
    });

    it("round-trips agent entries", []() {
        AdminAgentsReplyMessage reply = { 1, 0, { { 0x80000001, 2, "truck42", FINGERPRINT } } };
        AdminAgentsReplyMessage decoded;
        uint8_t buffer[512];
        size_t expected_size = MESSAGE_HEADER_SIZE + ADMIN_AGENTS_REPLY_FIXED_FIELDS_SIZE + ADMIN_AGENTS_REPLY_ENTRY_SIZE;
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = AdminAgentsReplyMessage_Encode(&reply, buffer, sizeof(buffer));
        decoded_ok = AdminAgentsReplyMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(encoded_size).toBe(expected_size);
        expect(decoded_ok).toBe(true);
        expect(decoded.count).toBe(1);
        expect(decoded.entries[0].peer_id).toBe((uint32_t)0x80000001);
        expect(decoded.entries[0].interface_count).toBe(2);
        expect((const char *)decoded.entries[0].agent_name).toBe("truck42");
        expect((const char *)decoded.entries[0].fingerprint_hex).toBe(FINGERPRINT);
    });
});

describe("admin_pin_add_message", []() {
    it("round-trips the agent name and fingerprint", []() {
        AdminPinAddMessage request = { "truck42", FINGERPRINT };
        AdminPinAddMessage decoded;
        uint8_t buffer[256];
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = AdminPinAddMessage_Encode(&request, buffer, sizeof(buffer));
        decoded_ok = AdminPinAddMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(encoded_size).toBe((size_t)(MESSAGE_HEADER_SIZE + ADMIN_PIN_ADD_BODY_SIZE));
        expect(decoded_ok).toBe(true);
        expect((const char *)decoded.agent_name).toBe("truck42");
        expect((const char *)decoded.fingerprint_hex).toBe(FINGERPRINT);
    });

    it("rejects encoding an empty name or fingerprint", []() {
        AdminPinAddMessage no_name = { "", FINGERPRINT };
        AdminPinAddMessage no_fp = { "truck42", "" };
        uint8_t buffer[256];

        expect(AdminPinAddMessage_Encode(&no_name, buffer, sizeof(buffer))).toBe((size_t)0);
        expect(AdminPinAddMessage_Encode(&no_fp, buffer, sizeof(buffer))).toBe((size_t)0);
    });

    it("round-trips the reply status", []() {
        AdminPinAddReplyMessage reply = { ADMIN_STATUS_PIN_FAILED };
        AdminPinAddReplyMessage decoded;
        uint8_t buffer[16];
        size_t encoded_size;

        encoded_size = AdminPinAddReplyMessage_Encode(&reply, buffer, sizeof(buffer));

        expect(AdminPinAddReplyMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE)).toBe(true);
        expect(decoded.status).toBe(ADMIN_STATUS_PIN_FAILED);
    });
});

describe("admin_acl_message", []() {
    it("round-trips an acl set with read and write permission", []() {
        AdminAclSetMessage request = { "truck42", "can0", FINGERPRINT, 1, 1 };
        AdminAclSetMessage decoded;
        uint8_t buffer[256];
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = AdminAclSetMessage_Encode(&request, buffer, sizeof(buffer));
        decoded_ok = AdminAclSetMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(encoded_size).toBe((size_t)(MESSAGE_HEADER_SIZE + ADMIN_ACL_SET_BODY_SIZE));
        expect(decoded_ok).toBe(true);
        expect((const char *)decoded.agent_name).toBe("truck42");
        expect((const char *)decoded.interface_name).toBe("can0");
        expect((const char *)decoded.fingerprint_hex).toBe(FINGERPRINT);
        expect(decoded.can_read).toBe(1);
        expect(decoded.can_write).toBe(1);
    });

    it("carries the none level and the wildcard scopes", []() {
        AdminAclSetMessage request = { "*", "*", "*", 0, 0 };
        AdminAclSetMessage decoded;
        uint8_t buffer[256];
        size_t encoded_size = AdminAclSetMessage_Encode(&request, buffer, sizeof(buffer));

        expect(AdminAclSetMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE)).toBe(true);
        expect((const char *)decoded.fingerprint_hex).toBe("*");
        expect((const char *)decoded.agent_name).toBe("*");
        expect(decoded.can_read).toBe(0);
        expect(decoded.can_write).toBe(0);
    });

    it("round-trips an acl revoke", []() {
        AdminAclRevokeMessage message = { "truck42", "can0", FINGERPRINT };
        AdminAclRevokeMessage decoded;
        uint8_t buffer[256];
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = AdminAclRevokeMessage_Encode(&message, buffer, sizeof(buffer));
        decoded_ok = AdminAclRevokeMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(decoded_ok).toBe(true);
        expect((const char *)decoded.interface_name).toBe("can0");
    });

    it("round-trips an acl revoke reply", []() {
        AdminAclRevokeReplyMessage message = { ADMIN_STATUS_UNKNOWN_AGENT };
        AdminAclRevokeReplyMessage decoded;
        uint8_t buffer[256];
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = AdminAclRevokeReplyMessage_Encode(&message, buffer, sizeof(buffer));
        decoded_ok = AdminAclRevokeReplyMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(decoded_ok).toBe(true);
        expect(decoded.status).toBe(ADMIN_STATUS_UNKNOWN_AGENT);
    });

    it("round-trips acl list entries", []() {
        AdminAclListReplyMessage reply = { 1, 0, { { "truck42", "can0", FINGERPRINT, 1, 1 } } };
        AdminAclListReplyMessage decoded;
        uint8_t buffer[512];
        size_t expected_size = MESSAGE_HEADER_SIZE + ADMIN_ACL_LIST_REPLY_FIXED_FIELDS_SIZE + ADMIN_ACL_LIST_REPLY_ENTRY_SIZE;
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = AdminAclListReplyMessage_Encode(&reply, buffer, sizeof(buffer));
        decoded_ok = AdminAclListReplyMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(encoded_size).toBe(expected_size);
        expect(decoded_ok).toBe(true);
        expect(decoded.count).toBe(1);
        expect((const char *)decoded.entries[0].agent_name).toBe("truck42");
        expect((const char *)decoded.entries[0].interface_name).toBe("can0");
        expect((const char *)decoded.entries[0].fingerprint_hex).toBe(FINGERPRINT);
        expect(decoded.entries[0].can_write).toBe(1);
    });

    it("round-trips a bridge set with its filters", []() {
        AdminBridgeSetMessage request = { { "truck42", "can0" }, { "bus7", "can0" }, 2, { { 0x100, 0x7FF }, { 0x200, 0x700 } } };
        AdminBridgeSetMessage decoded;
        uint8_t buffer[512];
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = AdminBridgeSetMessage_Encode(&request, buffer, sizeof(buffer));
        decoded_ok = AdminBridgeSetMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(encoded_size).toBe((size_t)(MESSAGE_HEADER_SIZE + ADMIN_BRIDGE_SET_BODY_SIZE));
        expect(decoded_ok).toBe(true);
        expect((const char *)decoded.source.agent_name).toBe("truck42");
        expect((const char *)decoded.destination.interface_name).toBe("can0");
        expect(decoded.filter_count).toBe(2);
        expect(decoded.filters[1].can_id).toBe((uint32_t)0x200);
        expect(decoded.filters[1].can_mask).toBe((uint32_t)0x700);
    });

    it("rejects decoding a bridge set with more filters than it can hold", []() {
        AdminBridgeSetMessage request = { { "truck42", "can0" }, { "bus7", "can0" }, 0, { { 0 } } };
        AdminBridgeSetMessage decoded;
        uint8_t buffer[512];
        size_t encoded_size;

        encoded_size = AdminBridgeSetMessage_Encode(&request, buffer, sizeof(buffer));
        buffer[MESSAGE_HEADER_SIZE + ADMIN_BRIDGE_ENDPOINT_SIZE * 2] = SUBSCRIBE_FILTERS_MAX + 1;

        expect(AdminBridgeSetMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE)).toBe(false);
    });

    it("round-trips a bridge delete", []() {
        AdminBridgeDeleteMessage request = { { "truck42", "can0" }, { "bus7", "can1" } };
        AdminBridgeDeleteMessage decoded;
        uint8_t buffer[512];
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = AdminBridgeDeleteMessage_Encode(&request, buffer, sizeof(buffer));
        decoded_ok = AdminBridgeDeleteMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(decoded_ok).toBe(true);
        expect((const char *)decoded.destination.interface_name).toBe("can1");
    });

    it("round-trips bridge list entries with their counters", []() {
        static AdminBridgeListReplyMessage reply;
        static AdminBridgeListReplyMessage decoded;
        uint8_t buffer[4096];
        size_t expected_size = MESSAGE_HEADER_SIZE + ADMIN_BRIDGE_LIST_REPLY_FIXED_FIELDS_SIZE + 2 * ADMIN_BRIDGE_LIST_REPLY_ENTRY_SIZE;
        size_t encoded_size;
        bool decoded_ok;

        memset(&reply, 0, sizeof(reply));
        reply.count = 2;
        reply.flags = ADMIN_REPLY_FLAG_MORE;
        reply.entries[0] = { { "truck42", "can0" }, { "bus7", "can0" }, 1, ADMIN_BRIDGE_STATE_SOURCE_ONLINE, { { 0x100, 0x7FF } }, 42, 3 };
        reply.entries[1] = { { "bus7", "can0" }, { "truck42", "can0" }, 0, 0, { { 0 } }, 0, 0 };

        encoded_size = AdminBridgeListReplyMessage_Encode(&reply, buffer, sizeof(buffer));
        decoded_ok = AdminBridgeListReplyMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(encoded_size).toBe(expected_size);
        expect(decoded_ok).toBe(true);
        expect(decoded.count).toBe(2);
        expect(decoded.flags).toBe((uint8_t)ADMIN_REPLY_FLAG_MORE);
        expect(decoded.entries[0].forwarded).toBe((uint64_t)42);
        expect(decoded.entries[0].dropped).toBe((uint64_t)3);
        expect(decoded.entries[0].state).toBe((uint8_t)ADMIN_BRIDGE_STATE_SOURCE_ONLINE);
        expect(decoded.entries[0].filters[0].can_mask).toBe((uint32_t)0x7FF);
        expect((const char *)decoded.entries[1].source.agent_name).toBe("bus7");
    });

    it("round-trips a group set with its filters", []() {
        AdminGroupSetMessage request = { "fleet", 1, { { 0x100, 0x7FF } } };
        AdminGroupSetMessage decoded;
        uint8_t buffer[512];
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = AdminGroupSetMessage_Encode(&request, buffer, sizeof(buffer));
        decoded_ok = AdminGroupSetMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(encoded_size).toBe((size_t)(MESSAGE_HEADER_SIZE + ADMIN_GROUP_SET_BODY_SIZE));
        expect(decoded_ok).toBe(true);
        expect((const char *)decoded.name).toBe("fleet");
        expect(decoded.filters[0].can_mask).toBe((uint32_t)0x7FF);
    });

    it("round-trips a group join", []() {
        AdminGroupMemberMessage request = { "fleet", { "truck42", "can0" } };
        AdminGroupMemberMessage decoded;
        MessageHeader header;
        uint8_t buffer[512];
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = AdminGroupMemberMessage_Encode(kMESSAGE_TYPE_ADMIN_GROUP_JOIN, &request, buffer, sizeof(buffer));
        MessageHeader_Decode(&header, buffer, encoded_size);
        decoded_ok = AdminGroupMemberMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(header.type).toBe((uint8_t)kMESSAGE_TYPE_ADMIN_GROUP_JOIN);
        expect(decoded_ok).toBe(true);
        expect((const char *)decoded.member.agent_name).toBe("truck42");
    });

    it("refuses to encode a member message of another type", []() {
        AdminGroupMemberMessage request = { "fleet", { "truck42", "can0" } };
        uint8_t buffer[512];

        expect(AdminGroupMemberMessage_Encode(kMESSAGE_TYPE_ADMIN_GROUP_SET, &request, buffer, sizeof(buffer))).toBe((size_t)0);
    });

    it("round-trips a group list entry with its members", []() {
        static AdminGroupListReplyMessage reply;
        static AdminGroupListReplyMessage decoded;
        uint8_t buffer[4096];
        size_t encoded_size;
        bool decoded_ok;

        memset(&reply, 0, sizeof(reply));
        reply.count = 1;
        snprintf(reply.entries[0].name, sizeof(reply.entries[0].name), "fleet");
        reply.entries[0].member_count = 2;
        reply.entries[0].forwarded = 9;
        reply.entries[0].members[0] = { { "truck42", "can0" }, ADMIN_GROUP_MEMBER_ONLINE };
        reply.entries[0].members[1] = { { "lab", "can1" }, 0 };

        encoded_size = AdminGroupListReplyMessage_Encode(&reply, buffer, sizeof(buffer));
        decoded_ok = AdminGroupListReplyMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(encoded_size).toBe((size_t)(MESSAGE_HEADER_SIZE + ADMIN_GROUP_LIST_REPLY_FIXED_FIELDS_SIZE + ADMIN_GROUP_LIST_REPLY_ENTRY_SIZE));
        expect(decoded_ok).toBe(true);
        expect(decoded.entries[0].forwarded).toBe((uint64_t)9);
        expect(decoded.entries[0].members[0].state).toBe((uint8_t)ADMIN_GROUP_MEMBER_ONLINE);
        expect((const char *)decoded.entries[0].members[1].endpoint.interface_name).toBe("can1");
    });

    it("rejects encoding an acl list entry with an unterminated agent name", []() {
        AdminAclListReplyMessage reply = { 1, 0, { { "truck42", "can0", FINGERPRINT, 1, 1 } } };
        uint8_t buffer[512];
        size_t encoded_size;

        memset(reply.entries[0].agent_name, 'a', REGISTER_AGENT_NAME_SIZE);

        encoded_size = AdminAclListReplyMessage_Encode(&reply, buffer, sizeof(buffer));

        expect(encoded_size).toBe((size_t)0);
    });

    it("rejects encoding an acl list entry with an unterminated interface name", []() {
        AdminAclListReplyMessage reply = { 1, 0, { { "truck42", "can0", FINGERPRINT, 1, 1 } } };
        uint8_t buffer[512];
        size_t encoded_size;

        memset(reply.entries[0].interface_name, 'a', REGISTER_INTERFACE_NAME_SIZE);

        encoded_size = AdminAclListReplyMessage_Encode(&reply, buffer, sizeof(buffer));

        expect(encoded_size).toBe((size_t)0);
    });

    it("rejects encoding an acl list entry with an unterminated fingerprint", []() {
        AdminAclListReplyMessage reply = { 1, 0, { { "truck42", "can0", FINGERPRINT, 1, 1 } } };
        uint8_t buffer[512];
        size_t encoded_size;

        memset(reply.entries[0].fingerprint_hex, 'a', ADMIN_FINGERPRINT_HEX_SIZE);

        encoded_size = AdminAclListReplyMessage_Encode(&reply, buffer, sizeof(buffer));

        expect(encoded_size).toBe((size_t)0);
    });
});

describe("admin_interfaces_message", []() {
    it("round-trips the pagination offset", []() {
        AdminInterfacesMessage request = { 9 };
        AdminInterfacesMessage decoded;
        uint8_t buffer[16];
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = AdminInterfacesMessage_Encode(&request, buffer, sizeof(buffer));
        decoded_ok = AdminInterfacesMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(encoded_size).toBe((size_t)(MESSAGE_HEADER_SIZE + ADMIN_INTERFACES_BODY_SIZE));
        expect(decoded_ok).toBe(true);
        expect(decoded.offset).toBe(9);
    });

    it("round-trips interface entries", []() {
        AdminInterfacesReplyMessage reply = { 1, 0, { { 7, 2, 5000000000ULL, 123456789ULL, "truck42", "can0", ADMIN_INTERFACE_MODE_FD } } };
        AdminInterfacesReplyMessage decoded;
        uint8_t buffer[512];
        size_t expected_size = MESSAGE_HEADER_SIZE + ADMIN_INTERFACES_REPLY_FIXED_FIELDS_SIZE + ADMIN_INTERFACES_REPLY_ENTRY_SIZE;
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = AdminInterfacesReplyMessage_Encode(&reply, buffer, sizeof(buffer));
        decoded_ok = AdminInterfacesReplyMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(encoded_size).toBe(expected_size);
        expect(decoded_ok).toBe(true);
        expect(decoded.count).toBe(1);
        expect(decoded.entries[0].interface_id).toBe((uint32_t)7);
        expect(decoded.entries[0].subscriber_count).toBe(2);
        expect(decoded.entries[0].frames_received).toBe(5000000000ULL);
        expect(decoded.entries[0].tx_dropped).toBe(123456789ULL);
        expect((const char *)decoded.entries[0].agent_name).toBe("truck42");
        expect((const char *)decoded.entries[0].interface_name).toBe("can0");
        expect(decoded.entries[0].mode).toBe((uint8_t)ADMIN_INTERFACE_MODE_FD);
    });

    it("rejects a payload shorter than its declared entries", []() {
        AdminInterfacesReplyMessage decoded;
        uint8_t payload[ADMIN_INTERFACES_REPLY_FIXED_FIELDS_SIZE + ADMIN_INTERFACES_REPLY_ENTRY_SIZE] = { 0 };

        payload[0] = 2;

        expect(AdminInterfacesReplyMessage_Decode(&decoded, payload, sizeof(payload))).toBe(false);
    });
});

describe("admin_clients_message", []() {
    it("round-trips client channel entries including the no-channel sentinel", []() {
        AdminClientsReplyMessage reply = { 2, 0, {
            { 0x40000003, 7, 0, "truck42", "can0", 1500, 7 },
            { 0x40000004, 0, ADMIN_CLIENT_NO_CHANNEL, "", "", 0, 0 },
        } };
        AdminClientsReplyMessage decoded;
        uint8_t buffer[512];
        size_t expected_size = MESSAGE_HEADER_SIZE + ADMIN_CLIENTS_REPLY_FIXED_FIELDS_SIZE + 2 * ADMIN_CLIENTS_REPLY_ENTRY_SIZE;
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = AdminClientsReplyMessage_Encode(&reply, buffer, sizeof(buffer));
        decoded_ok = AdminClientsReplyMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(encoded_size).toBe(expected_size);
        expect(decoded_ok).toBe(true);
        expect(decoded.count).toBe(2);
        expect(decoded.entries[0].peer_id).toBe((uint32_t)0x40000003);
        expect(decoded.entries[0].interface_id).toBe((uint32_t)7);
        expect(decoded.entries[0].channel).toBe(0);
        expect((const char *)decoded.entries[0].agent_name).toBe("truck42");
        expect((const char *)decoded.entries[0].interface_name).toBe("can0");
        expect(decoded.entries[0].frames_forwarded).toBe((uint32_t)1500);
        expect(decoded.entries[0].frames_dropped).toBe((uint32_t)7);
        expect(decoded.entries[1].channel).toBe(ADMIN_CLIENT_NO_CHANNEL);
        expect((const char *)decoded.entries[1].agent_name).toBe("");
    });

    it("rejects a payload shorter than its declared entries", []() {
        AdminClientsReplyMessage decoded;
        uint8_t payload[ADMIN_CLIENTS_REPLY_FIXED_FIELDS_SIZE + ADMIN_CLIENTS_REPLY_ENTRY_SIZE] = { 0 };

        payload[0] = 2;

        expect(AdminClientsReplyMessage_Decode(&decoded, payload, sizeof(payload))).toBe(false);
    });
});

describe("admin_rejected_message", []() {
    it("round-trips the pagination offset", []() {
        AdminRejectedMessage request = { 15 };
        AdminRejectedMessage decoded;
        uint8_t buffer[16];
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = AdminRejectedMessage_Encode(&request, buffer, sizeof(buffer));
        decoded_ok = AdminRejectedMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(encoded_size).toBe((size_t)(MESSAGE_HEADER_SIZE + ADMIN_REJECTED_BODY_SIZE));
        expect(decoded_ok).toBe(true);
        expect(decoded.offset).toBe((uint16_t)15);
    });

    it("round-trips rejected entries", []() {
        AdminRejectedReplyMessage reply = {
            1, ADMIN_REPLY_FLAG_MORE, { { 1, ADMIN_REJECTED_REASON_FINGERPRINT_MISMATCH, 12, 8, "truck42", FINGERPRINT, "203.0.113.5:40000" } }
        };
        AdminRejectedReplyMessage decoded;
        uint8_t buffer[512];
        size_t expected_size = MESSAGE_HEADER_SIZE + ADMIN_REJECTED_REPLY_FIXED_FIELDS_SIZE + ADMIN_REJECTED_REPLY_ENTRY_SIZE;
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = AdminRejectedReplyMessage_Encode(&reply, buffer, sizeof(buffer));
        decoded_ok = AdminRejectedReplyMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(encoded_size).toBe(expected_size);
        expect(decoded_ok).toBe(true);
        expect(decoded.count).toBe(1);
        expect(decoded.flags).toBe(ADMIN_REPLY_FLAG_MORE);
        expect(decoded.entries[0].role).toBe(1);
        expect(decoded.entries[0].reason).toBe(ADMIN_REJECTED_REASON_FINGERPRINT_MISMATCH);
        expect(decoded.entries[0].attempts).toBe((uint32_t)12);
        expect(decoded.entries[0].last_seen_seconds).toBe((uint32_t)8);
        expect((const char *)decoded.entries[0].name).toBe("truck42");
        expect((const char *)decoded.entries[0].fingerprint_hex).toBe(FINGERPRINT);
        expect((const char *)decoded.entries[0].origin).toBe("203.0.113.5:40000");
    });

    it("rejects encoding a rejected entry with an unterminated origin", []() {
        AdminRejectedReplyMessage reply = { 1, 0, { { 1, 0, 1, 0, "truck42", FINGERPRINT, "" } } };
        uint8_t buffer[512];
        size_t encoded_size;

        memset(reply.entries[0].origin, 'a', ADMIN_ORIGIN_SIZE);

        encoded_size = AdminRejectedReplyMessage_Encode(&reply, buffer, sizeof(buffer));

        expect(encoded_size).toBe((size_t)0);
    });

    it("rejects a reply that claims more entries than it carries", []() {
        AdminRejectedReplyMessage decoded;
        uint8_t payload[ADMIN_REJECTED_REPLY_FIXED_FIELDS_SIZE] = { 2, 0, 0, 0 };

        expect(AdminRejectedReplyMessage_Decode(&decoded, payload, sizeof(payload))).toBe(false);
    });
});
