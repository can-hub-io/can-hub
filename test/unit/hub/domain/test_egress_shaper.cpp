#include <cest>

extern "C" {
#include "hub/domain/egress_shaper.h"
}

describe("egress_shaper", []() {
    it("is a passthrough when the rate is zero", []() {
        EgressShaper shaper;

        EgressShaper_Init(&shaper, 0, 0, 1000);

        expect(EgressShaper_TryConsume(&shaper, 1000000)).toBe(true);
        expect(EgressShaper_DelayUs(&shaper, 1000000)).toBe((uint64_t)0);
    });

    it("consumes only within the bucket", []() {
        EgressShaper shaper;

        EgressShaper_Init(&shaper, 0, 1000, 1000);

        expect(EgressShaper_TryConsume(&shaper, 600)).toBe(true);
        expect(EgressShaper_TryConsume(&shaper, 600)).toBe(false);
    });

    it("refills proportionally to elapsed time", []() {
        EgressShaper shaper;

        EgressShaper_Init(&shaper, 0, 1000, 1000);
        EgressShaper_TryConsume(&shaper, 1000);
        EgressShaper_Refill(&shaper, 500000);

        expect(EgressShaper_TryConsume(&shaper, 500)).toBe(true);
        expect(EgressShaper_TryConsume(&shaper, 1)).toBe(false);
    });

    it("clamps accrued credit to the burst cap", []() {
        EgressShaper shaper;

        EgressShaper_Init(&shaper, 0, 1000, 1000);
        EgressShaper_TryConsume(&shaper, 1000);
        EgressShaper_Refill(&shaper, 10000000);

        expect(EgressShaper_TryConsume(&shaper, 1000)).toBe(true);
        expect(EgressShaper_TryConsume(&shaper, 1)).toBe(false);
    });

    it("carries the sub-bit remainder so the rate does not drift", []() {
        EgressShaper shaper;

        EgressShaper_Init(&shaper, 0, 1000, 10000);
        EgressShaper_TryConsume(&shaper, 10000);
        EgressShaper_Refill(&shaper, 1500);
        EgressShaper_Refill(&shaper, 3000);

        expect(EgressShaper_TryConsume(&shaper, 3)).toBe(true);
        expect(EgressShaper_TryConsume(&shaper, 1)).toBe(false);
    });

    it("reports the delay until enough credit accrues", []() {
        EgressShaper shaper;

        EgressShaper_Init(&shaper, 0, 1000, 1000);
        EgressShaper_TryConsume(&shaper, 1000);

        expect(EgressShaper_DelayUs(&shaper, 500)).toBe((uint64_t)500000);
        expect(EgressShaper_DelayUs(&shaper, 0)).toBe((uint64_t)0);
    });

    it("starts pacing once a rate is set on an unpaced shaper", []() {
        EgressShaper shaper;

        EgressShaper_Init(&shaper, 0, 0, 1000);
        EgressShaper_SetRate(&shaper, 1000);

        expect(EgressShaper_TryConsume(&shaper, 1000)).toBe(true);
        expect(EgressShaper_TryConsume(&shaper, 1)).toBe(false);
    });
});
