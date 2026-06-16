#include <cest>

extern "C" {
#include "hub/domain/egress_queue.h"
}

#include <string.h>

static EgressQueue queue;

static uint16_t pushChannel(uint8_t channel, uint8_t marker);
static uint8_t frontMarker(uint8_t channel);
static uint16_t fillChannel(uint8_t channel, uint16_t count);

describe("egress_queue", []() {
    beforeEach([]() {
        EgressQueue_Reset(&queue);
    });

    it("queues a frame and reports it pending", []() {
        TEGRESS_PUSH_RESULT result;
        uint8_t evicted = 0;
        uint8_t payload[4] = {1, 2, 3, 4};

        result = EgressQueue_Push(&queue, 5, payload, sizeof(payload), 0, &evicted);

        expect(result == kEGRESS_PUSH_QUEUED).toBe(true);
        expect(EgressQueue_HasPending(&queue)).toBe(true);
        expect(EgressQueue_ChannelPending(&queue, 5)).toBe(true);
        expect(EgressQueue_ChannelPending(&queue, 6)).toBe(false);
    });

    it("drains a channel in FIFO order", []() {
        uint8_t first;
        uint8_t second;

        pushChannel(5, 0xA1);
        pushChannel(5, 0xB2);

        first = frontMarker(5);
        EgressQueue_PopChannel(&queue, 5);
        second = frontMarker(5);
        EgressQueue_PopChannel(&queue, 5);

        expect(first).toBe((uint8_t)0xA1);
        expect(second).toBe((uint8_t)0xB2);
        expect(EgressQueue_HasPending(&queue)).toBe(false);
    });

    it("drops the oldest of its own channel at the per-channel cap", []() {
        uint8_t evicted = 0;
        TEGRESS_PUSH_RESULT result;
        uint8_t payload[1] = {0xFF};
        uint16_t i;

        for(i=0; i<EGRESS_QUEUE_CHANNEL_CAP; i++) {
            pushChannel(5, (uint8_t)i);
        }
        result = EgressQueue_Push(&queue, 5, payload, sizeof(payload), 0, &evicted);

        expect(result == kEGRESS_PUSH_EVICTED_SELF).toBe(true);
        expect(evicted).toBe((uint8_t)5);
        expect(queue.channels[5].count).toBe((uint16_t)EGRESS_QUEUE_CHANNEL_CAP);
        expect(frontMarker(5)).toBe((uint8_t)1);
    });

    it("evicts the fullest channel when the pool is exhausted under cap", []() {
        uint8_t evicted = 0;
        TEGRESS_PUSH_RESULT result;
        uint8_t payload[2] = {0xDE, 0xAD};
        uint8_t channel;

        for(channel=10; channel<10 + EGRESS_QUEUE_SLOTS_MAX / EGRESS_QUEUE_CHANNEL_CAP; channel++) {
            fillChannel(channel, EGRESS_QUEUE_CHANNEL_CAP);
        }

        expect(queue.used).toBe((uint16_t)EGRESS_QUEUE_SLOTS_MAX);

        result = EgressQueue_Push(&queue, 3, payload, sizeof(payload), 0, &evicted);

        expect(result == kEGRESS_PUSH_EVICTED_OTHER).toBe(true);
        expect(EgressQueue_ChannelPending(&queue, 3)).toBe(true);
        expect(evicted != 3).toBe(true);
    });

    it("round-robins across channels with pending frames", []() {
        uint8_t a = 0;
        uint8_t b = 0;
        uint8_t c = 0;
        bool got_a;
        bool got_b;
        bool got_c;

        pushChannel(5, 0);
        pushChannel(6, 0);

        got_a = EgressQueue_NextPendingChannel(&queue, &a);
        got_b = EgressQueue_NextPendingChannel(&queue, &b);
        got_c = EgressQueue_NextPendingChannel(&queue, &c);

        expect(got_a).toBe(true);
        expect(got_b).toBe(true);
        expect(got_c).toBe(true);
        expect(a != b).toBe(true);
        expect(c == a).toBe(true);
    });

    it("a busy channel at cap never steals a quiet channel's queued frame", []() {
        uint8_t quiet_marker;
        uint16_t i;

        pushChannel(2, 0x42);
        for(i=0; i<EGRESS_QUEUE_CHANNEL_CAP * 4; i++) {
            pushChannel(9, (uint8_t)i);
        }

        quiet_marker = frontMarker(2);

        expect(EgressQueue_ChannelPending(&queue, 2)).toBe(true);
        expect(quiet_marker).toBe((uint8_t)0x42);
    });
});

static uint16_t pushChannel(uint8_t channel, uint8_t marker)
{
    uint8_t payload[EGRESS_FRAME_BYTES_MAX];
    uint8_t evicted = 0;

    memset(payload, 0, sizeof(payload));
    payload[0] = marker;
    EgressQueue_Push(&queue, channel, payload, 1, 0, &evicted);

    return marker;
}

static uint8_t frontMarker(uint8_t channel)
{
    uint16_t size = 0;
    const uint8_t *front = EgressQueue_FrontOfChannel(&queue, channel, &size);

    if (front == NULL) {
        return 0;
    }

    return front[0];
}

static uint16_t fillChannel(uint8_t channel, uint16_t count)
{
    uint16_t i;

    for(i=0; i<count; i++) {
        pushChannel(channel, (uint8_t)i);
    }

    return count;
}
