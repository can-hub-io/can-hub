#include <cest>

#include <cstring>

extern "C" {
#include "protocol/admin_message.h"
#include "protocol/message_header.h"
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
        AdminStatusReplyMessage reply = { 5, 2, 3, 8 };
        AdminStatusReplyMessage decoded;
        uint8_t buffer[32];
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
            { 0x80000001, 1, "truck42", FINGERPRINT },
            { 0x40000001, 3, "", "" },
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
        expect(decoded.entries[0].role).toBe(1);
        expect((const char *)decoded.entries[0].agent_name).toBe("truck42");
        expect((const char *)decoded.entries[0].fingerprint_hex).toBe(FINGERPRINT);
        expect(decoded.entries[1].role).toBe(3);
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

    it("round-trips the forget request and reply", []() {
        AdminForgetMessage forget = { "truck42" };
        AdminForgetMessage decoded_forget;
        AdminForgetReplyMessage reply = { ADMIN_STATUS_OK };
        AdminForgetReplyMessage decoded_reply;
        uint8_t buffer[256];
        size_t encoded_size;

        encoded_size = AdminForgetMessage_Encode(&forget, buffer, sizeof(buffer));
        expect(AdminForgetMessage_Decode(&decoded_forget, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE)).toBe(true);
        expect((const char *)decoded_forget.agent_name).toBe("truck42");

        encoded_size = AdminForgetReplyMessage_Encode(&reply, buffer, sizeof(buffer));
        expect(AdminForgetReplyMessage_Decode(&decoded_reply, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE)).toBe(true);
        expect(decoded_reply.status).toBe(ADMIN_STATUS_OK);
    });
});
