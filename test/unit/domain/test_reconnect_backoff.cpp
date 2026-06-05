#include <cest>

extern "C" {
#include "domain/reconnect_backoff.h"
}

describe("reconnect_backoff", []() {
    it("returns the initial delay first", []() {
        ReconnectBackoff backoff;
        uint32_t delay_ms;

        ReconnectBackoff_Init(&backoff, 1000, 60000);
        delay_ms = ReconnectBackoff_NextDelayMs(&backoff);

        expect(delay_ms).toBe((uint32_t)1000);
    });

    it("doubles the delay on each retry", []() {
        ReconnectBackoff backoff;
        uint32_t first_delay_ms;
        uint32_t second_delay_ms;
        uint32_t third_delay_ms;

        ReconnectBackoff_Init(&backoff, 1000, 60000);
        first_delay_ms = ReconnectBackoff_NextDelayMs(&backoff);
        second_delay_ms = ReconnectBackoff_NextDelayMs(&backoff);
        third_delay_ms = ReconnectBackoff_NextDelayMs(&backoff);

        expect(first_delay_ms).toBe((uint32_t)1000);
        expect(second_delay_ms).toBe((uint32_t)2000);
        expect(third_delay_ms).toBe((uint32_t)4000);
    });

    it("caps the delay at the configured maximum", []() {
        ReconnectBackoff backoff;
        uint32_t delay_ms = 0;
        uint8_t i;

        ReconnectBackoff_Init(&backoff, 1000, 60000);
        for(i=0; i<10; i++) {
            delay_ms = ReconnectBackoff_NextDelayMs(&backoff);
        }

        expect(delay_ms).toBe((uint32_t)60000);
    });

    it("returns the initial delay again after a reset", []() {
        ReconnectBackoff backoff;
        uint32_t delay_ms;

        ReconnectBackoff_Init(&backoff, 1000, 60000);
        ReconnectBackoff_NextDelayMs(&backoff);
        ReconnectBackoff_NextDelayMs(&backoff);

        ReconnectBackoff_Reset(&backoff);
        delay_ms = ReconnectBackoff_NextDelayMs(&backoff);

        expect(delay_ms).toBe((uint32_t)1000);
    });
});
