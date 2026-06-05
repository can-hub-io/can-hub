#include <cest>

extern "C" {
#include "protocol/message_header.h"
}

describe("message_header", []() {
    it("round-trips type, flags and length", []() {
        MessageHeader header = { kMESSAGE_TYPE_FRAME, 0x01, 0x1234 };
        MessageHeader decoded;
        uint8_t buffer[MESSAGE_HEADER_SIZE];
        size_t expected_size = MESSAGE_HEADER_SIZE;
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = MessageHeader_Encode(&header, buffer, sizeof(buffer));
        decoded_ok = MessageHeader_Decode(&decoded, buffer, sizeof(buffer));

        expect(encoded_size).toBe(expected_size);
        expect(decoded_ok).toBe(true);
        expect(decoded.type).toBe(kMESSAGE_TYPE_FRAME);
        expect(decoded.flags).toBe(0x01);
        expect(decoded.length).toBe(0x1234);
    });

    it("encodes length little-endian", []() {
        MessageHeader header = { kMESSAGE_TYPE_PING, 0, 0x0102 };
        uint8_t buffer[MESSAGE_HEADER_SIZE];

        MessageHeader_Encode(&header, buffer, sizeof(buffer));

        expect(buffer[2]).toBe(0x02);
        expect(buffer[3]).toBe(0x01);
    });

    it("rejects a buffer smaller than the header", []() {
        MessageHeader header = { kMESSAGE_TYPE_PING, 0, 0 };
        MessageHeader decoded;
        uint8_t buffer[MESSAGE_HEADER_SIZE];
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = MessageHeader_Encode(&header, buffer, MESSAGE_HEADER_SIZE - 1);
        decoded_ok = MessageHeader_Decode(&decoded, buffer, MESSAGE_HEADER_SIZE - 1);

        expect(encoded_size).toBe((size_t)0);
        expect(decoded_ok).toBe(false);
    });
});
