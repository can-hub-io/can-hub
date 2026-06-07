#include <cest>

extern "C" {
#include "protocol/message_header.h"
#include "protocol/open_message.h"
}

describe("open_message", []() {
    it("round-trips the interface id and the flags", []() {
        OpenMessage open = { 0xCAFE0007, OPEN_FLAG_SUPPRESS_OWN_ECHO };
        OpenMessage decoded;
        uint8_t buffer[16];
        size_t expected_size = MESSAGE_HEADER_SIZE + OPEN_BODY_SIZE;
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = OpenMessage_Encode(&open, buffer, sizeof(buffer));
        decoded_ok = OpenMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, OPEN_BODY_SIZE);

        expect(encoded_size).toBe(expected_size);
        expect(decoded_ok).toBe(true);
        expect(decoded.interface_id).toBe((uint32_t)0xCAFE0007);
        expect(decoded.flags).toBe(OPEN_FLAG_SUPPRESS_OWN_ECHO);
    });

    it("rejects a truncated open payload", []() {
        OpenMessage decoded;
        uint8_t payload[OPEN_BODY_SIZE] = { 0 };
        bool decoded_ok;

        decoded_ok = OpenMessage_Decode(&decoded, payload, OPEN_BODY_SIZE - 1);

        expect(decoded_ok).toBe(false);
    });
});

describe("open_ack_message", []() {
    it("round-trips status, channel and interface id", []() {
        OpenAckMessage ack = { OPEN_STATUS_OK, 5, 0xCAFE0007 };
        OpenAckMessage decoded;
        uint8_t buffer[16];
        size_t expected_size = MESSAGE_HEADER_SIZE + OPEN_ACK_BODY_SIZE;
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = OpenAckMessage_Encode(&ack, buffer, sizeof(buffer));
        decoded_ok = OpenAckMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, OPEN_ACK_BODY_SIZE);

        expect(encoded_size).toBe(expected_size);
        expect(decoded_ok).toBe(true);
        expect(decoded.status).toBe(OPEN_STATUS_OK);
        expect(decoded.channel).toBe(5);
        expect(decoded.interface_id).toBe((uint32_t)0xCAFE0007);
    });

    it("rejects a truncated ack payload", []() {
        OpenAckMessage decoded;
        uint8_t payload[OPEN_ACK_BODY_SIZE] = { 0 };
        bool decoded_ok;

        decoded_ok = OpenAckMessage_Decode(&decoded, payload, OPEN_ACK_BODY_SIZE - 1);

        expect(decoded_ok).toBe(false);
    });
});

describe("close_message", []() {
    it("round-trips the channel", []() {
        CloseMessage close = { 5 };
        CloseMessage decoded;
        uint8_t buffer[16];
        size_t expected_size = MESSAGE_HEADER_SIZE + CLOSE_BODY_SIZE;
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = CloseMessage_Encode(&close, buffer, sizeof(buffer));
        decoded_ok = CloseMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, CLOSE_BODY_SIZE);

        expect(encoded_size).toBe(expected_size);
        expect(decoded_ok).toBe(true);
        expect(decoded.channel).toBe(5);
    });

    it("rejects a truncated close payload", []() {
        CloseMessage decoded;
        uint8_t payload[CLOSE_BODY_SIZE] = { 0 };
        bool decoded_ok;

        decoded_ok = CloseMessage_Decode(&decoded, payload, CLOSE_BODY_SIZE - 1);

        expect(decoded_ok).toBe(false);
    });
});
