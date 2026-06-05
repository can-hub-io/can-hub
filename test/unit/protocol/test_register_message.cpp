#include <cest>

#include <cstring>

extern "C" {
#include "protocol/message_header.h"
#include "protocol/register_message.h"
}

describe("register_message", []() {
    it("round-trips agent name and interface names", []() {
        RegisterMessage registration = { "truck42", 2, { "can0", "can1" } };
        RegisterMessage decoded;
        uint8_t buffer[512];
        size_t expected_size = MESSAGE_HEADER_SIZE + REGISTER_BODY_SIZE;
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = RegisterMessage_Encode(&registration, buffer, sizeof(buffer));
        decoded_ok = RegisterMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, REGISTER_BODY_SIZE);

        expect(encoded_size).toBe(expected_size);
        expect(decoded_ok).toBe(true);
        expect((const char *)decoded.agent_name).toBe("truck42");
        expect(decoded.interface_count).toBe(2);
        expect((const char *)decoded.interface_names[0]).toBe("can0");
        expect((const char *)decoded.interface_names[1]).toBe("can1");
    });

    it("rejects too many interfaces", []() {
        RegisterMessage registration = { "truck42", REGISTER_INTERFACES_MAX + 1, { "can0" } };
        uint8_t buffer[512];
        size_t encoded_size;

        encoded_size = RegisterMessage_Encode(&registration, buffer, sizeof(buffer));

        expect(encoded_size).toBe((size_t)0);
    });

    it("rejects an empty interface name", []() {
        RegisterMessage registration = { "truck42", 2, { "can0", "" } };
        uint8_t buffer[512];
        size_t encoded_size;

        encoded_size = RegisterMessage_Encode(&registration, buffer, sizeof(buffer));

        expect(encoded_size).toBe((size_t)0);
    });

    it("rejects a truncated payload", []() {
        RegisterMessage decoded;
        uint8_t payload[REGISTER_BODY_SIZE] = { 0 };
        bool decoded_ok;

        decoded_ok = RegisterMessage_Decode(&decoded, payload, REGISTER_BODY_SIZE - 1);

        expect(decoded_ok).toBe(false);
    });

    it("round-trips an ack with channels", []() {
        RegisterAckMessage ack = { REGISTER_STATUS_OK, 2, { 1, 2 } };
        RegisterAckMessage decoded;
        uint8_t buffer[64];
        size_t expected_size = MESSAGE_HEADER_SIZE + REGISTER_ACK_BODY_SIZE;
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = RegisterAckMessage_Encode(&ack, buffer, sizeof(buffer));
        decoded_ok = RegisterAckMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, REGISTER_ACK_BODY_SIZE);

        expect(encoded_size).toBe(expected_size);
        expect(decoded_ok).toBe(true);
        expect(decoded.status).toBe(REGISTER_STATUS_OK);
        expect(decoded.interface_count).toBe(2);
        expect(decoded.channels[0]).toBe(1);
        expect(decoded.channels[1]).toBe(2);
    });

    it("rejects an ack with too many interfaces", []() {
        RegisterAckMessage decoded;
        uint8_t payload[REGISTER_ACK_BODY_SIZE] = { 0 };
        bool decoded_ok;

        payload[1] = REGISTER_INTERFACES_MAX + 1;

        decoded_ok = RegisterAckMessage_Decode(&decoded, payload, REGISTER_ACK_BODY_SIZE);

        expect(decoded_ok).toBe(false);
    });
});
