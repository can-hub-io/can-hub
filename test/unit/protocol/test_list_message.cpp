#include <cest>

#include <cstring>

extern "C" {
#include "protocol/list_message.h"
#include "protocol/message_header.h"
}

describe("list_message", []() {
    it("round-trips the pagination offset", []() {
        ListMessage list = { 32 };
        ListMessage decoded;
        uint8_t buffer[16];
        size_t expected_size = MESSAGE_HEADER_SIZE + LIST_BODY_SIZE;
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = ListMessage_Encode(&list, buffer, sizeof(buffer));
        decoded_ok = ListMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, LIST_BODY_SIZE);

        expect(encoded_size).toBe(expected_size);
        expect(decoded_ok).toBe(true);
        expect(decoded.offset).toBe(32);
    });

    it("rejects a truncated list payload", []() {
        ListMessage decoded;
        uint8_t payload[LIST_BODY_SIZE] = { 0 };
        bool decoded_ok;

        decoded_ok = ListMessage_Decode(&decoded, payload, LIST_BODY_SIZE - 1);

        expect(decoded_ok).toBe(false);
    });
});

describe("list_reply_message", []() {
    it("round-trips entries with the more flag", []() {
        ListReplyMessage reply = { 2, LIST_REPLY_FLAG_MORE, { { 7, "truck42", "can0" }, { 9, "truck42", "can1" } } };
        ListReplyMessage decoded;
        uint8_t buffer[512];
        size_t expected_size = MESSAGE_HEADER_SIZE + LIST_REPLY_FIXED_FIELDS_SIZE + 2 * LIST_REPLY_ENTRY_SIZE;
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = ListReplyMessage_Encode(&reply, buffer, sizeof(buffer));
        decoded_ok = ListReplyMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(encoded_size).toBe(expected_size);
        expect(decoded_ok).toBe(true);
        expect(decoded.count).toBe(2);
        expect(decoded.flags).toBe(LIST_REPLY_FLAG_MORE);
        expect(decoded.entries[0].interface_id).toBe((uint32_t)7);
        expect((const char *)decoded.entries[0].agent_name).toBe("truck42");
        expect((const char *)decoded.entries[0].interface_name).toBe("can0");
        expect(decoded.entries[1].interface_id).toBe((uint32_t)9);
        expect((const char *)decoded.entries[1].interface_name).toBe("can1");
    });

    it("round-trips an empty reply", []() {
        ListReplyMessage reply = { 0, 0, { } };
        ListReplyMessage decoded;
        uint8_t buffer[64];
        size_t expected_size = MESSAGE_HEADER_SIZE + LIST_REPLY_FIXED_FIELDS_SIZE;
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = ListReplyMessage_Encode(&reply, buffer, sizeof(buffer));
        decoded_ok = ListReplyMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(encoded_size).toBe(expected_size);
        expect(decoded_ok).toBe(true);
        expect(decoded.count).toBe(0);
    });

    it("rejects more entries than the maximum", []() {
        ListReplyMessage reply = { LIST_REPLY_ENTRIES_MAX + 1, 0, { } };
        uint8_t buffer[4096];
        size_t encoded_size;

        encoded_size = ListReplyMessage_Encode(&reply, buffer, sizeof(buffer));

        expect(encoded_size).toBe((size_t)0);
    });

    it("rejects a payload shorter than its declared entries", []() {
        ListReplyMessage decoded;
        uint8_t payload[LIST_REPLY_FIXED_FIELDS_SIZE + LIST_REPLY_ENTRY_SIZE] = { 0 };
        bool decoded_ok;

        payload[0] = 2;

        decoded_ok = ListReplyMessage_Decode(&decoded, payload, sizeof(payload));

        expect(decoded_ok).toBe(false);
    });
});
