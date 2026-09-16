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

    it("walks every frame packed in one datagram", []() {
        FrameMessage first = { 0x101, 11, 1, 2, 0, 0, { 0xAA, 0xBB } };
        FrameMessage second = { 0x202, 22, 2, 3, 0, 0, { 0x01, 0x02, 0x03 } };
        FrameMessage third = { 0x303, 33, 3, 1, 0, 0, { 0x7F } };
        FrameMessage decoded;
        FrameStream stream;
        uint8_t buffer[256];
        size_t packed = 0;
        uint32_t seen[4];
        uint8_t count = 0;

        packed += FrameMessage_Encode(&first, buffer + packed, sizeof(buffer) - packed);
        packed += FrameMessage_Encode(&second, buffer + packed, sizeof(buffer) - packed);
        packed += FrameMessage_Encode(&third, buffer + packed, sizeof(buffer) - packed);

        FrameStream_Init(&stream, buffer, packed);
        while (FrameStream_Next(&stream, &decoded)) {
            seen[count] = decoded.can_id;
            count++;
        }

        expect(count).toBe((uint8_t)3);
        expect(seen[0]).toBe((uint32_t)0x101);
        expect(seen[1]).toBe((uint32_t)0x202);
        expect(seen[2]).toBe((uint32_t)0x303);
    });

    it("keeps the frames before a truncated tail", []() {
        FrameMessage first = { 0x101, 11, 1, 2, 0, 0, { 0xAA, 0xBB } };
        FrameMessage second = { 0x202, 22, 2, 3, 0, 0, { 0x01, 0x02, 0x03 } };
        FrameMessage decoded;
        FrameStream stream;
        uint8_t buffer[256];
        size_t packed = 0;
        uint8_t count = 0;

        packed += FrameMessage_Encode(&first, buffer + packed, sizeof(buffer) - packed);
        packed += FrameMessage_Encode(&second, buffer + packed, sizeof(buffer) - packed);

        FrameStream_Init(&stream, buffer, packed - 1);
        while (FrameStream_Next(&stream, &decoded)) {
            count++;
        }

        expect(count).toBe((uint8_t)1);
    });

    it("yields the single frame a lone datagram carries", []() {
        FrameMessage only = { 0x123, 1, 1, 4, 0, 0, { 1, 2, 3, 4 } };
        FrameMessage decoded;
        FrameStream stream;
        uint8_t buffer[128];
        size_t encoded_size;
        bool first_ok;
        bool second_ok;

        encoded_size = FrameMessage_Encode(&only, buffer, sizeof(buffer));

        FrameStream_Init(&stream, buffer, encoded_size);
        first_ok = FrameStream_Next(&stream, &decoded);
        second_ok = FrameStream_Next(&stream, &decoded);

        expect(first_ok).toBe(true);
        expect(second_ok).toBe(false);
        expect(decoded.can_id).toBe((uint32_t)0x123);
    });

    it("yields nothing from an empty datagram", []() {
        FrameMessage decoded;
        FrameStream stream;
        uint8_t buffer[1] = { 0 };

        FrameStream_Init(&stream, buffer, 0);

        expect(FrameStream_Next(&stream, &decoded)).toBe(false);
    });

    it("stops at a packed entry that is not a frame", []() {
        FrameMessage first = { 0x101, 11, 1, 2, 0, 0, { 0xAA, 0xBB } };
        FrameMessage decoded;
        FrameStream stream;
        MessageHeader ping = { kMESSAGE_TYPE_PING, 0, 0 };
        uint8_t buffer[256];
        size_t packed = 0;
        uint8_t count = 0;

        packed += FrameMessage_Encode(&first, buffer + packed, sizeof(buffer) - packed);
        packed += MessageHeader_Encode(&ping, buffer + packed, sizeof(buffer) - packed);

        FrameStream_Init(&stream, buffer, packed);
        while (FrameStream_Next(&stream, &decoded)) {
            count++;
        }

        expect(count).toBe((uint8_t)1);
    });
});
