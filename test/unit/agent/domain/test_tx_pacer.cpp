#include <cest>

extern "C" {
#include "agent/domain/tx_pacer.h"
}

describe("tx_pacer", []() {
    it("starts at the advertised rate on the first window", []() {
        TxPacer pacer;

        TxPacer_Reset(&pacer);

        expect(TxPacer_Update(&pacer, 0, 100000, 0)).toBe((uint32_t)100000);
    });

    it("backs off multiplicatively when new drops appear", []() {
        TxPacer pacer;

        TxPacer_Reset(&pacer);
        TxPacer_Update(&pacer, 0, 100000, 0);

        expect(TxPacer_Update(&pacer, 0, 100000, 5)).toBe((uint32_t)80000);
        expect(TxPacer_Update(&pacer, 0, 100000, 9)).toBe((uint32_t)64000);
    });

    it("ramps back additively when no new drops appear", []() {
        TxPacer pacer;

        TxPacer_Reset(&pacer);
        TxPacer_Update(&pacer, 0, 100000, 0);
        TxPacer_Update(&pacer, 0, 100000, 5);

        expect(TxPacer_Update(&pacer, 0, 100000, 5)).toBe((uint32_t)92500);
        expect(TxPacer_Update(&pacer, 0, 100000, 5)).toBe((uint32_t)100000);
    });

    it("never falls below the floor", []() {
        TxPacer pacer;
        uint64_t drops = 0;
        uint32_t credit;
        int window;

        TxPacer_Reset(&pacer);
        TxPacer_Update(&pacer, 0, 160000, drops);
        credit = 160000;
        for(window=0; window<40; window++) {
            drops += 1;
            credit = TxPacer_Update(&pacer, 0, 160000, drops);
        }

        expect(credit).toBe((uint32_t)10000);
    });

    it("stays unpaced when the advertised rate is zero", []() {
        TxPacer pacer;

        TxPacer_Reset(&pacer);
        TxPacer_Update(&pacer, 0, 0, 0);

        expect(TxPacer_Update(&pacer, 0, 0, 3)).toBe((uint32_t)0);
    });

    it("tracks interfaces independently", []() {
        TxPacer pacer;

        TxPacer_Reset(&pacer);
        TxPacer_Update(&pacer, 0, 100000, 0);
        TxPacer_Update(&pacer, 1, 100000, 0);
        TxPacer_Update(&pacer, 0, 100000, 7);

        expect(TxPacer_Update(&pacer, 1, 100000, 0)).toBe((uint32_t)100000);
    });
});
