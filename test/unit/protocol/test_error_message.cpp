#include <cest>

#include <cstring>

extern "C" {
#include "protocol/error_message.h"
#include "protocol/message_header.h"
}

describe("error_message", []() {
    it("round-trips code and detail", []() {
        ErrorMessage error = { 0x0203, "bus not found" };
        ErrorMessage decoded;
        uint8_t buffer[128];
        size_t expected_size = MESSAGE_HEADER_SIZE + ERROR_BODY_SIZE;
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = ErrorMessage_Encode(&error, buffer, sizeof(buffer));
        decoded_ok = ErrorMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, ERROR_BODY_SIZE);

        expect(encoded_size).toBe(expected_size);
        expect(decoded_ok).toBe(true);
        expect(decoded.code).toBe(0x0203);
        expect((const char *)decoded.detail).toBe("bus not found");
    });

    it("always NUL-terminates the decoded detail", []() {
        ErrorMessage decoded;
        uint8_t payload[ERROR_BODY_SIZE];
        bool decoded_ok;

        memset(payload, 'A', sizeof(payload));

        decoded_ok = ErrorMessage_Decode(&decoded, payload, sizeof(payload));

        expect(decoded_ok).toBe(true);
        expect(decoded.detail[ERROR_DETAIL_SIZE - 1]).toBe('\0');
    });

    it("rejects a truncated payload", []() {
        ErrorMessage decoded;
        uint8_t payload[ERROR_BODY_SIZE] = { 0 };
        bool decoded_ok;

        decoded_ok = ErrorMessage_Decode(&decoded, payload, ERROR_BODY_SIZE - 1);

        expect(decoded_ok).toBe(false);
    });
});
