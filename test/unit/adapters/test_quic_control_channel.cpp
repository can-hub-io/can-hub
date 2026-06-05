#include <cest>

extern "C" {
#include "adapters/quic/quic_control_channel.h"
#include "protocol/message_header.h"
}

static QuicControlChannel channel;

describe("quic_control_channel", []() {
    beforeEach([]() {
        QuicControlChannel_Reset(&channel);
    });

    it("retains queued tx bytes until acked", []() {
        uint8_t first_chunk[4] = { 1, 2, 3, 4 };
        uint8_t second_chunk[2] = { 5, 6 };
        const uint8_t *pending;
        size_t pending_size;

        QuicControlChannel_QueueTx(&channel, first_chunk, sizeof(first_chunk));
        QuicControlChannel_QueueTx(&channel, second_chunk, sizeof(second_chunk));
        pending_size = QuicControlChannel_PendingTx(&channel, &pending);

        expect(pending_size).toBe((size_t)6);
        expect(pending).toEqualMemory((const uint8_t *)"\x01\x02\x03\x04\x05\x06", 6);
    });

    it("excludes sent bytes from the pending region", []() {
        uint8_t chunk[4] = { 1, 2, 3, 4 };
        const uint8_t *pending;
        size_t pending_size;

        QuicControlChannel_QueueTx(&channel, chunk, sizeof(chunk));
        QuicControlChannel_MarkSent(&channel, 3);
        pending_size = QuicControlChannel_PendingTx(&channel, &pending);

        expect(pending_size).toBe((size_t)1);
        expect(pending[0]).toBe(4);
    });

    it("releases acked bytes and keeps stream offsets consistent", []() {
        uint8_t chunk[4] = { 1, 2, 3, 4 };
        const uint8_t *pending;
        size_t pending_size;

        QuicControlChannel_QueueTx(&channel, chunk, sizeof(chunk));
        QuicControlChannel_MarkSent(&channel, 4);
        QuicControlChannel_MarkAcked(&channel, 2);
        pending_size = QuicControlChannel_PendingTx(&channel, &pending);

        expect(pending_size).toBe((size_t)0);
        expect(channel.tx_used).toBe((size_t)2);
        expect(channel.tx_base_offset).toBe((uint64_t)2);
    });

    it("rejects tx bytes beyond the buffer capacity", []() {
        uint8_t chunk[QUIC_CONTROL_TX_BUFFER_SIZE] = { 0 };
        bool first_queued;
        bool second_queued;

        first_queued = QuicControlChannel_QueueTx(&channel, chunk, sizeof(chunk));
        second_queued = QuicControlChannel_QueueTx(&channel, chunk, 1);

        expect(first_queued).toBe(true);
        expect(second_queued).toBe(false);
    });

    it("reassembles a message split across rx chunks", []() {
        MessageHeader header = { kMESSAGE_TYPE_PING, 0, 4 };
        uint8_t message[MESSAGE_HEADER_SIZE + 4] = { 0 };
        const uint8_t *next_message;
        size_t first_attempt_size;
        size_t second_attempt_size;

        MessageHeader_Encode(&header, message, sizeof(message));

        QuicControlChannel_QueueRx(&channel, message, 5);
        first_attempt_size = QuicControlChannel_NextMessage(&channel, &next_message);
        QuicControlChannel_QueueRx(&channel, message + 5, sizeof(message) - 5);
        second_attempt_size = QuicControlChannel_NextMessage(&channel, &next_message);

        expect(first_attempt_size).toBe((size_t)0);
        expect(second_attempt_size).toBe(sizeof(message));
        expect(next_message).toEqualMemory(message, sizeof(message));
    });

    it("consumes a message and exposes the next one", []() {
        MessageHeader header = { kMESSAGE_TYPE_PING, 0, 0 };
        uint8_t message[MESSAGE_HEADER_SIZE] = { 0 };
        const uint8_t *next_message;
        size_t remaining_size;

        MessageHeader_Encode(&header, message, sizeof(message));

        QuicControlChannel_QueueRx(&channel, message, sizeof(message));
        QuicControlChannel_QueueRx(&channel, message, sizeof(message));
        QuicControlChannel_ConsumeMessage(&channel, sizeof(message));
        remaining_size = QuicControlChannel_NextMessage(&channel, &next_message);

        expect(remaining_size).toBe(sizeof(message));
        expect(channel.rx_used).toBe(sizeof(message));
    });
});
