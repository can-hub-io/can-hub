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
        uint8_t first_token = 0;
        uint8_t second_token = 0;
        bool first_matched;
        bool second_matched;

        EchoCorrelator_Push(&correlator, INTERFACE_A, 7, 0x100);
        EchoCorrelator_Push(&correlator, INTERFACE_A, 9, 0x200);

        first_matched = EchoCorrelator_PopMatch(&correlator, INTERFACE_A, 0x100, &first_token);
        second_matched = EchoCorrelator_PopMatch(&correlator, INTERFACE_A, 0x200, &second_token);

        expect(first_matched).toBe(true);
        expect(first_token).toBe(7);
        expect(second_matched).toBe(true);
        expect(second_token).toBe(9);
    });

    it("discards lost entries older than the match", []() {
        uint8_t token = 0;
        bool newer_matched;
        bool older_matched;

        EchoCorrelator_Push(&correlator, INTERFACE_A, 7, 0x100);
        EchoCorrelator_Push(&correlator, INTERFACE_A, 9, 0x200);

        newer_matched = EchoCorrelator_PopMatch(&correlator, INTERFACE_A, 0x200, &token);
        older_matched = EchoCorrelator_PopMatch(&correlator, INTERFACE_A, 0x100, &token);

        expect(newer_matched).toBe(true);
        expect(token).toBe(9);
        expect(older_matched).toBe(false);
    });

    it("drops the newest entry after a failed write", []() {
        uint8_t token = 0;
        bool newest_matched;
        bool oldest_matched;

        EchoCorrelator_Push(&correlator, INTERFACE_A, 7, 0x100);
        EchoCorrelator_Push(&correlator, INTERFACE_A, 9, 0x200);

        EchoCorrelator_DropNewest(&correlator, INTERFACE_A);
        newest_matched = EchoCorrelator_PopMatch(&correlator, INTERFACE_A, 0x200, &token);
        oldest_matched = EchoCorrelator_PopMatch(&correlator, INTERFACE_A, 0x100, &token);

        expect(newest_matched).toBe(false);
        expect(oldest_matched).toBe(true);
        expect(token).toBe(7);
    });

    it("keeps interfaces independent", []() {
        uint8_t b_token = 0;
        uint8_t a_token = 0;
        bool b_matched;
        bool b_matched_again;
        bool a_matched;

        EchoCorrelator_Push(&correlator, INTERFACE_A, 7, 0x100);
        EchoCorrelator_Push(&correlator, INTERFACE_B, 9, 0x100);

        b_matched = EchoCorrelator_PopMatch(&correlator, INTERFACE_B, 0x100, &b_token);
        b_matched_again = EchoCorrelator_PopMatch(&correlator, INTERFACE_B, 0x100, &b_token);
        a_matched = EchoCorrelator_PopMatch(&correlator, INTERFACE_A, 0x100, &a_token);

        expect(b_matched).toBe(true);
        expect(b_token).toBe(9);
        expect(b_matched_again).toBe(false);
        expect(a_matched).toBe(true);
        expect(a_token).toBe(7);
    });

    it("misses when nothing was pushed", []() {
        uint8_t token = 0;

        expect(EchoCorrelator_PopMatch(&correlator, INTERFACE_A, 0x100, &token)).toBe(false);
    });

    it("evicts the oldest entry when full", []() {
        uint8_t token = 0;
        bool oldest_matched;
        bool next_matched;
        uint8_t i;

        for(i=0; i<ECHO_CORRELATOR_PENDING_MAX + 1; i++) {
            EchoCorrelator_Push(&correlator, INTERFACE_A, i, 0x100 + i);
        }

        oldest_matched = EchoCorrelator_PopMatch(&correlator, INTERFACE_A, 0x100, &token);
        next_matched = EchoCorrelator_PopMatch(&correlator, INTERFACE_A, 0x101, &token);

        expect(oldest_matched).toBe(false);
        expect(next_matched).toBe(true);
        expect(token).toBe(1);
    });
});
