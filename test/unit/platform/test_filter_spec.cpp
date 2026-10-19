#include <cest>

extern "C" {
#include "platform/linux/shared/filter_spec.h"
#include "protocol/frame_message.h"
}

#define FILTER_TEXT_SIZE 24

static CanFilter filter;
static char text[FILTER_TEXT_SIZE];

describe("FilterSpec_Parse", []() {
    it("takes an id alone as an exact match", []() {
        bool parsed = FilterSpec_Parse("123", &filter);

        expect(parsed).toBe(true);
        expect(filter.can_id).toBe((uint32_t)0x123);
        expect(filter.can_mask).toBe((uint32_t)FRAME_CAN_ID_MASK);
    });

    it("takes an id with a mask", []() {
        bool parsed = FilterSpec_Parse("100:700", &filter);

        expect(parsed).toBe(true);
        expect(filter.can_id).toBe((uint32_t)0x100);
        expect(filter.can_mask).toBe((uint32_t)0x700);
    });

    it("rejects text that is not hex", []() {
        expect(FilterSpec_Parse("12g", &filter)).toBe(false);
        expect(FilterSpec_Parse("100:", &filter)).toBe(false);
    });
});

describe("FilterSpec_Format", []() {
    it("prints an exact match as the id alone", []() {
        CanFilter exact = { 0x123, FRAME_CAN_ID_MASK };

        FilterSpec_Format(&exact, text, sizeof(text));

        expect((const char *)text).toBe("123");
    });

    it("prints a masked filter as id:mask", []() {
        CanFilter ranged = { 0x100, 0x700 };

        FilterSpec_Format(&ranged, text, sizeof(text));

        expect((const char *)text).toBe("100:700");
    });
});
