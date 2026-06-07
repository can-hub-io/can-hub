#include <cest>

extern "C" {
#include "protocol/message_header.h"
#include "protocol/subscribe_message.h"
}

describe("subscribe_message", []() {
    it("round-trips the channel and the filter list", []() {
        SubscribeMessage subscribe;
        SubscribeMessage decoded;
        uint8_t buffer[MESSAGE_HEADER_SIZE + SUBSCRIBE_FIXED_FIELDS_SIZE + 2 * CAN_FILTER_SIZE];
        size_t expected_size = sizeof(buffer);
        size_t encoded_size;
        bool decoded_ok;

        subscribe.channel = 3;
        subscribe.filter_count = 2;
        subscribe.filters[0] = { 0x123, 0x7FF };
        subscribe.filters[1] = { 0x1ABCDEF0, 0x1FFFFFFF };

        encoded_size = SubscribeMessage_Encode(&subscribe, buffer, sizeof(buffer));
        decoded_ok = SubscribeMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(encoded_size).toBe(expected_size);
        expect(decoded_ok).toBe(true);
        expect(decoded.channel).toBe(3);
        expect(decoded.filter_count).toBe(2);
        expect(decoded.filters[0].can_id).toBe((uint32_t)0x123);
        expect(decoded.filters[0].can_mask).toBe((uint32_t)0x7FF);
        expect(decoded.filters[1].can_id).toBe((uint32_t)0x1ABCDEF0);
        expect(decoded.filters[1].can_mask).toBe((uint32_t)0x1FFFFFFF);
    });

    it("round-trips an empty filter list", []() {
        SubscribeMessage subscribe;
        SubscribeMessage decoded;
        uint8_t buffer[MESSAGE_HEADER_SIZE + SUBSCRIBE_FIXED_FIELDS_SIZE];
        size_t encoded_size;
        bool decoded_ok;

        subscribe.channel = 9;
        subscribe.filter_count = 0;

        encoded_size = SubscribeMessage_Encode(&subscribe, buffer, sizeof(buffer));
        decoded_ok = SubscribeMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(encoded_size).toBe((size_t)(MESSAGE_HEADER_SIZE + SUBSCRIBE_FIXED_FIELDS_SIZE));
        expect(decoded_ok).toBe(true);
        expect(decoded.channel).toBe(9);
        expect(decoded.filter_count).toBe(0);
    });

    it("rejects a truncated fixed header", []() {
        SubscribeMessage decoded;
        uint8_t payload[SUBSCRIBE_FIXED_FIELDS_SIZE] = { 0 };
        bool decoded_ok;

        decoded_ok = SubscribeMessage_Decode(&decoded, payload, SUBSCRIBE_FIXED_FIELDS_SIZE - 1);

        expect(decoded_ok).toBe(false);
    });

    it("rejects a payload shorter than the declared filter count", []() {
        SubscribeMessage decoded;
        uint8_t payload[SUBSCRIBE_FIXED_FIELDS_SIZE + CAN_FILTER_SIZE] = { 0 };
        bool decoded_ok;

        payload[1] = 2;

        decoded_ok = SubscribeMessage_Decode(&decoded, payload, sizeof(payload));

        expect(decoded_ok).toBe(false);
    });

    it("rejects a filter count beyond the maximum", []() {
        SubscribeMessage decoded;
        uint8_t payload[SUBSCRIBE_FIXED_FIELDS_SIZE] = { 0 };
        bool decoded_ok;

        payload[1] = SUBSCRIBE_FILTERS_MAX + 1;

        decoded_ok = SubscribeMessage_Decode(&decoded, payload, SUBSCRIBE_FIXED_FIELDS_SIZE);

        expect(decoded_ok).toBe(false);
    });
});
