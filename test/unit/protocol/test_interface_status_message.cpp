#include <cest>

extern "C" {
#include "protocol/interface_status_message.h"
#include "protocol/message_header.h"
}

describe("interface_status_message", []() {
    it("round-trips per-interface tx-drop counters", []() {
        InterfaceStatusMessage status;
        InterfaceStatusMessage decoded;
        uint8_t buffer[MESSAGE_HEADER_SIZE + INTERFACE_STATUS_BODY_SIZE];
        size_t encoded_size;
        bool decoded_ok;

        memset(&status, 0, sizeof(status));
        status.interface_count = 2;
        status.entries[0].channel = 3;
        status.entries[0].tx_dropped = 5000000000ULL;
        status.entries[1].channel = 7;
        status.entries[1].tx_dropped = 42;

        encoded_size = InterfaceStatusMessage_Encode(&status, buffer, sizeof(buffer));
        decoded_ok = InterfaceStatusMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, INTERFACE_STATUS_BODY_SIZE);

        expect(encoded_size).toBe((size_t)(MESSAGE_HEADER_SIZE + INTERFACE_STATUS_BODY_SIZE));
        expect(decoded_ok).toBe(true);
        expect(decoded.interface_count).toBe(2);
        expect(decoded.entries[0].channel).toBe(3);
        expect(decoded.entries[0].tx_dropped).toBe(5000000000ULL);
        expect(decoded.entries[1].channel).toBe(7);
        expect(decoded.entries[1].tx_dropped).toBe((uint64_t)42);
    });

    it("preserves the reserved pacing fields", []() {
        InterfaceStatusMessage status;
        InterfaceStatusMessage decoded;
        uint8_t buffer[MESSAGE_HEADER_SIZE + INTERFACE_STATUS_BODY_SIZE];

        memset(&status, 0, sizeof(status));
        status.interface_count = 1;
        status.entries[0].channel = 1;
        status.entries[0].flags = INTERFACE_STATUS_FLAG_RELIABLE;
        status.entries[0].advertised_rate = 500000;
        status.entries[0].credit = 1234;
        status.entries[0].tx_dropped = 9;

        InterfaceStatusMessage_Encode(&status, buffer, sizeof(buffer));
        InterfaceStatusMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, INTERFACE_STATUS_BODY_SIZE);

        expect(decoded.entries[0].flags).toBe(INTERFACE_STATUS_FLAG_RELIABLE);
        expect(decoded.entries[0].advertised_rate).toBe((uint32_t)500000);
        expect(decoded.entries[0].credit).toBe((uint32_t)1234);
    });

    it("rejects a truncated payload", []() {
        InterfaceStatusMessage decoded;
        uint8_t payload[INTERFACE_STATUS_BODY_SIZE] = { 0 };

        expect(InterfaceStatusMessage_Decode(&decoded, payload, INTERFACE_STATUS_BODY_SIZE - 1)).toBe(false);
    });

    it("rejects an out-of-range interface count", []() {
        InterfaceStatusMessage decoded;
        uint8_t payload[INTERFACE_STATUS_BODY_SIZE] = { 0 };

        payload[0] = REGISTER_INTERFACES_MAX + 1;
        expect(InterfaceStatusMessage_Decode(&decoded, payload, INTERFACE_STATUS_BODY_SIZE)).toBe(false);
    });
});
