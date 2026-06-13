#include <cest>

extern "C" {
#include "protocol/hello_message.h"
#include "protocol/message_header.h"
}

describe("hello_message", []() {
    it("round-trips version, role, capabilities and name", []() {
        HelloMessage hello = { PROTOCOL_VERSION, kPEER_ROLE_CLIENT, 0xCAFE0001, "dashboard" };
        HelloMessage decoded;
        uint8_t buffer[128];
        size_t expected_size = MESSAGE_HEADER_SIZE + HELLO_BODY_SIZE;
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = HelloMessage_Encode(&hello, buffer, sizeof(buffer));
        decoded_ok = HelloMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, HELLO_BODY_SIZE);

        expect(encoded_size).toBe(expected_size);
        expect(decoded_ok).toBe(true);
        expect(decoded.version).toBe(PROTOCOL_VERSION);
        expect(decoded.role).toBe(kPEER_ROLE_CLIENT);
        expect(decoded.capabilities).toBe((uint32_t)0xCAFE0001);
        expect((const char *)decoded.name).toBe("dashboard");
    });

    it("rejects encoding an invalid role", []() {
        HelloMessage hello = { PROTOCOL_VERSION, 0, 0, "" };
        uint8_t buffer[32];
        size_t encoded_size;

        encoded_size = HelloMessage_Encode(&hello, buffer, sizeof(buffer));

        expect(encoded_size).toBe((size_t)0);
    });

    it("rejects decoding an invalid role", []() {
        HelloMessage decoded;
        uint8_t payload[HELLO_BODY_SIZE] = { PROTOCOL_VERSION, kPEER_ROLE_MAX, 0, 0, 0, 0, 0, 0 };
        bool decoded_ok;

        decoded_ok = HelloMessage_Decode(&decoded, payload, HELLO_BODY_SIZE);

        expect(decoded_ok).toBe(false);
    });

    it("rejects a truncated payload", []() {
        HelloMessage decoded;
        uint8_t payload[HELLO_BODY_SIZE] = { 0 };
        bool decoded_ok;

        decoded_ok = HelloMessage_Decode(&decoded, payload, HELLO_BODY_SIZE - 1);

        expect(decoded_ok).toBe(false);
    });
});
