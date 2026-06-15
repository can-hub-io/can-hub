#include <cest>

#include <cstring>

extern "C" {
#include "platform/linux/quic/quic_datagram_backlog.h"
}

static QuicDatagramBacklog backlog;

static bool pushByte(uint8_t value);
static uint8_t frontByte();

describe("quic_datagram_backlog", []() {
    beforeEach([]() {
        QuicDatagramBacklog_Reset(&backlog);
    });

    it("starts empty", []() {
        expect(QuicDatagramBacklog_IsEmpty(&backlog)).toBe(true);
    });

    it("returns a pushed datagram by value then empties on pop", []() {
        const uint8_t payload[] = { 0xDE, 0xAD, 0xBE, 0xEF };
        const uint8_t *front;
        size_t size = 0;

        expect(QuicDatagramBacklog_Push(&backlog, payload, sizeof(payload))).toBe(true);
        front = QuicDatagramBacklog_Front(&backlog, &size);

        expect(size).toBe((size_t)sizeof(payload));
        expect(memcmp(front, payload, sizeof(payload)) == 0).toBe(true);

        QuicDatagramBacklog_PopFront(&backlog);
        expect(QuicDatagramBacklog_IsEmpty(&backlog)).toBe(true);
    });

    it("preserves fifo order", []() {
        pushByte(1);
        pushByte(2);
        pushByte(3);

        expect((int)frontByte()).toBe(1);
        QuicDatagramBacklog_PopFront(&backlog);
        expect((int)frontByte()).toBe(2);
        QuicDatagramBacklog_PopFront(&backlog);
        expect((int)frontByte()).toBe(3);
    });

    it("rejects a push once full", []() {
        int slot;

        for(slot=0; slot<QUIC_DATAGRAM_BACKLOG_SLOTS; slot++) {
            expect(pushByte((uint8_t)slot)).toBe(true);
        }

        expect(pushByte(0)).toBe(false);
    });

    it("rejects a datagram larger than a frame", []() {
        uint8_t oversized[FRAME_WIRE_SIZE + 1];

        memset(oversized, 0, sizeof(oversized));

        expect(QuicDatagramBacklog_Push(&backlog, oversized, sizeof(oversized))).toBe(false);
    });

    it("keeps fifo order across ring wraparound", []() {
        int cycle;

        for(cycle=0; cycle<QUIC_DATAGRAM_BACKLOG_SLOTS * 3; cycle++) {
            expect(pushByte((uint8_t)cycle)).toBe(true);
            expect((int)frontByte()).toBe((uint8_t)cycle);
            QuicDatagramBacklog_PopFront(&backlog);
        }

        expect(QuicDatagramBacklog_IsEmpty(&backlog)).toBe(true);
    });
});

static bool pushByte(uint8_t value)
{
    return QuicDatagramBacklog_Push(&backlog, &value, 1);
}

static uint8_t frontByte()
{
    size_t size = 0;
    const uint8_t *front = QuicDatagramBacklog_Front(&backlog, &size);

    return front[0];
}
