#include <cest>

#include <cstring>
#include <vector>

extern "C" {
#include "platform/linux/shared/message_framer.h"
#include "protocol/message_header.h"
}

static MessageFramer framer;
static size_t drained_count;

static size_t buildMessage(uint8_t *out, uint8_t payload_byte, uint16_t payload_length);
static size_t drainBurstInChunks(const uint8_t *data, size_t size, size_t chunk_size);
static void feedChunk(const uint8_t *data, size_t size, const MessageSink *sink);
static void countMessage(void *context, const uint8_t *message, size_t size);

describe("message_framer", []() {
    beforeEach([]() {
        MessageFramer_Reset(&framer);
        drained_count = 0;
    });

    it("push takes only what fits the buffer", []() {
        std::vector<uint8_t> blob(MESSAGE_FRAMER_BUFFER_SIZE + 100, 0);

        size_t taken = MessageFramer_Push(&framer, blob.data(), blob.size());

        expect(taken).toBe((size_t)MESSAGE_FRAMER_BUFFER_SIZE);
    });

    it("drain delivers complete messages and consumes them", []() {
        uint8_t message[64];
        size_t size = buildMessage(message, 0xAB, 8);
        MessageSink sink = { NULL, countMessage };

        MessageFramer_Push(&framer, message, size);
        MessageFramer_Push(&framer, message, size);
        MessageFramer_Drain(&framer, &sink);

        expect(drained_count).toBe((size_t)2);
        expect(framer.used).toBe((size_t)0);
    });

    it("extracts every message from a burst larger than the buffer", []() {
        uint8_t message[64];
        size_t size = buildMessage(message, 0x5A, 8);
        std::vector<uint8_t> burst;
        size_t want = 0;

        while (burst.size() < (size_t)MESSAGE_FRAMER_BUFFER_SIZE * 8) {
            burst.insert(burst.end(), message, message + size);
            want++;
        }

        size_t extracted = drainBurstInChunks(burst.data(), burst.size(), 2048);

        expect(extracted).toBe(want);
    });
});

static size_t buildMessage(uint8_t *out, uint8_t payload_byte, uint16_t payload_length)
{
    MessageHeader header = { kMESSAGE_TYPE_FRAME, 0, payload_length };
    size_t header_size = MessageHeader_Encode(&header, out, MESSAGE_HEADER_SIZE);

    memset(out + header_size, payload_byte, payload_length);

    return header_size + payload_length;
}

static size_t drainBurstInChunks(const uint8_t *data, size_t size, size_t chunk_size)
{
    MessageSink sink = { NULL, countMessage };
    size_t offset = 0;
    size_t chunk;

    while (offset < size) {
        chunk = size - offset < chunk_size ? size - offset : chunk_size;
        feedChunk(data + offset, chunk, &sink);
        offset += chunk;
    }

    return drained_count;
}

static void feedChunk(const uint8_t *data, size_t size, const MessageSink *sink)
{
    size_t offset = 0;
    size_t taken;

    while (offset < size) {
        taken = MessageFramer_Push(&framer, data + offset, size - offset);
        offset += taken;
        MessageFramer_Drain(&framer, sink);
        if (taken == 0) {
            return;
        }
    }
}

static void countMessage(void *context, const uint8_t *message, size_t size)
{
    (void)context;
    (void)message;
    (void)size;
    drained_count++;
}
