#include <cest>

#include <cstring>

extern "C" {
#include "agent/domain/echo_correlator.h"
}

#define INTERFACE_A 0
#define INTERFACE_B 1

static EchoCorrelator correlator;

describe("echo_correlator", []() {
    beforeEach([]() {
        EchoCorrelator_Reset(&correlator);
    });

    it("matches an echo with its token in order", []() {
        uint8_t token = 0;

        EchoCorrelator_Push(&correlator, INTERFACE_A, 7, 0x100);
        EchoCorrelator_Push(&correlator, INTERFACE_A, 9, 0x200);

        expect(EchoCorrelator_PopMatch(&correlator, INTERFACE_A, 0x100, &token)).toBe(true);
        expect(token).toBe(7);
        expect(EchoCorrelator_PopMatch(&correlator, INTERFACE_A, 0x200, &token)).toBe(true);
        expect(token).toBe(9);
    });

    it("misses when nothing was pushed", []() {
        uint8_t token = 0;

        expect(EchoCorrelator_PopMatch(&correlator, INTERFACE_A, 0x100, &token)).toBe(false);
    });

    it("discards lost entries older than the match", []() {
        uint8_t token = 0;

        EchoCorrelator_Push(&correlator, INTERFACE_A, 7, 0x100);
        EchoCorrelator_Push(&correlator, INTERFACE_A, 9, 0x200);

        expect(EchoCorrelator_PopMatch(&correlator, INTERFACE_A, 0x200, &token)).toBe(true);
        expect(token).toBe(9);
        expect(EchoCorrelator_PopMatch(&correlator, INTERFACE_A, 0x100, &token)).toBe(false);
    });

    it("drops the newest entry after a failed write", []() {
        uint8_t token = 0;

        EchoCorrelator_Push(&correlator, INTERFACE_A, 7, 0x100);
        EchoCorrelator_Push(&correlator, INTERFACE_A, 9, 0x200);
        EchoCorrelator_DropNewest(&correlator, INTERFACE_A);

        expect(EchoCorrelator_PopMatch(&correlator, INTERFACE_A, 0x200, &token)).toBe(false);
        expect(EchoCorrelator_PopMatch(&correlator, INTERFACE_A, 0x100, &token)).toBe(true);
        expect(token).toBe(7);
    });

    it("keeps interfaces independent", []() {
        uint8_t token = 0;

        EchoCorrelator_Push(&correlator, INTERFACE_A, 7, 0x100);
        EchoCorrelator_Push(&correlator, INTERFACE_B, 9, 0x100);

        expect(EchoCorrelator_PopMatch(&correlator, INTERFACE_B, 0x100, &token)).toBe(true);
        expect(token).toBe(9);
        expect(EchoCorrelator_PopMatch(&correlator, INTERFACE_B, 0x100, &token)).toBe(false);
        expect(EchoCorrelator_PopMatch(&correlator, INTERFACE_A, 0x100, &token)).toBe(true);
        expect(token).toBe(7);
    });

    it("evicts the oldest entry when full", []() {
        uint8_t token = 0;
        uint8_t i;

        for(i=0; i<ECHO_CORRELATOR_PENDING_MAX + 1; i++) {
            EchoCorrelator_Push(&correlator, INTERFACE_A, i, 0x100 + i);
        }

        expect(EchoCorrelator_PopMatch(&correlator, INTERFACE_A, 0x100, &token)).toBe(false);
        expect(EchoCorrelator_PopMatch(&correlator, INTERFACE_A, 0x101, &token)).toBe(true);
        expect(token).toBe(1);
    });
});
