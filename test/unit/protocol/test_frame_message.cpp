#include <cest>

extern "C" {
#include "protocol/frame_message.h"
#include "protocol/message_header.h"
}

describe("frame_message", []() {
    it("round-trips a classic frame", []() {
        FrameMessage frame = {
            0x123,
            1700000000123456ULL,
            7,
            4,
            0,
            FRAME_ROUTE_FLAG_ECHO | (9 << FRAME_ROUTE_TOKEN_SHIFT),
            { 0xDE, 0xAD, 0xBE, 0xEF },
        };
        FrameMessage decoded;
        uint8_t buffer[128];
        size_t expected_size = MESSAGE_HEADER_SIZE + FRAME_FIXED_FIELDS_SIZE + 4;
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = FrameMessage_Encode(&frame, buffer, sizeof(buffer));
        decoded_ok = FrameMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(encoded_size).toBe(expected_size);
        expect(decoded_ok).toBe(true);
        expect(decoded.can_id).toBe((uint32_t)0x123);
        expect(decoded.timestamp_us).toBe(1700000000123456ULL);
        expect(decoded.channel).toBe(7);
        expect(decoded.payload_length).toBe(4);
        expect(decoded.route_flags).toBe(FRAME_ROUTE_FLAG_ECHO | (9 << FRAME_ROUTE_TOKEN_SHIFT));
        expect(decoded.payload).toEqualMemory(frame.payload, 4);
    });

    it("round-trips a 64-byte FD frame", []() {
        FrameMessage frame = { 0x123, 1, 1, FRAME_PAYLOAD_MAX_FD, FRAME_FLAG_FD | FRAME_FLAG_BRS, 0, { 0 } };
        FrameMessage decoded;
        uint8_t buffer[128];
        size_t expected_size = MESSAGE_HEADER_SIZE + FRAME_FIXED_FIELDS_SIZE + FRAME_PAYLOAD_MAX_FD;
        size_t encoded_size;
        bool decoded_ok;
        uint8_t i;

        for(i=0; i<FRAME_PAYLOAD_MAX_FD; i++) {
            frame.payload[i] = i;
        }

        encoded_size = FrameMessage_Encode(&frame, buffer, sizeof(buffer));
        decoded_ok = FrameMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(encoded_size).toBe(expected_size);
        expect(decoded_ok).toBe(true);
        expect(decoded.payload_length).toBe(FRAME_PAYLOAD_MAX_FD);
        expect(decoded.payload).toEqualMemory(frame.payload, FRAME_PAYLOAD_MAX_FD);
    });

    it("rejects a classic frame with more than 8 payload bytes", []() {
        FrameMessage frame = { 0x123, 1, 1, 9, 0, 0, { 0 } };
        uint8_t buffer[128];
        size_t encoded_size;

        encoded_size = FrameMessage_Encode(&frame, buffer, sizeof(buffer));

        expect(encoded_size).toBe((size_t)0);
    });

    it("rejects decoding when the payload is truncated", []() {
        FrameMessage frame = { 0x123, 1, 1, 4, 0, 0, { 1, 2, 3, 4 } };
        FrameMessage decoded;
        uint8_t buffer[128];
        size_t encoded_size;
        size_t truncated_size;
        bool decoded_ok;

        encoded_size = FrameMessage_Encode(&frame, buffer, sizeof(buffer));
        truncated_size = encoded_size - MESSAGE_HEADER_SIZE - 1;
        decoded_ok = FrameMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, truncated_size);

        expect(decoded_ok).toBe(false);
    });

    it("rejects encoding into a too small buffer", []() {
        FrameMessage frame = { 0x123, 1, 1, 4, 0, 0, { 1, 2, 3, 4 } };
        uint8_t buffer[16];
        size_t encoded_size;

        encoded_size = FrameMessage_Encode(&frame, buffer, sizeof(buffer));

        expect(encoded_size).toBe((size_t)0);
    });
});
