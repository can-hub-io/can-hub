#include <cest>
#include <cstring>

extern "C" {
#include "agent/domain/step_ring.h"
}

#define RING_CAPACITY 64
#define RECORD_SIZE 24

static StepRing ring;
static uint8_t storage[RING_CAPACITY];

static void fillRecord(uint8_t *record, uint8_t marker);
static uint8_t peekMarker(void);

describe("step_ring", []() {
    beforeEach([]() {
        memset(storage, 0, sizeof(storage));
        StepRing_Init(&ring, storage, RING_CAPACITY);
    });

    it("starts empty", []() {
        const uint8_t *record;
        uint32_t available;

        available = StepRing_Peek(&ring, &record);

        expect(available).toBe((uint32_t)0);
        expect(StepRing_IsEmpty(&ring)).toBe(true);
    });

    it("hands records back in arrival order", []() {
        uint8_t first[RECORD_SIZE];
        uint8_t second[RECORD_SIZE];
        uint8_t first_marker;
        uint8_t second_marker;

        fillRecord(first, 1);
        fillRecord(second, 2);
        StepRing_Push(&ring, first, RECORD_SIZE);
        StepRing_Push(&ring, second, RECORD_SIZE);

        first_marker = peekMarker();
        StepRing_Release(&ring, RECORD_SIZE);
        second_marker = peekMarker();

        expect(first_marker).toBe(1);
        expect(second_marker).toBe(2);
    });

    it("refuses a record that does not fit anywhere", []() {
        uint8_t record[RECORD_SIZE];
        bool third_pushed;

        fillRecord(record, 1);
        StepRing_Push(&ring, record, RECORD_SIZE);
        StepRing_Push(&ring, record, RECORD_SIZE);

        third_pushed = StepRing_Push(&ring, record, RECORD_SIZE);

        expect(third_pushed).toBe(false);
    });

    it("wraps a record that does not fit before the end, keeping the order", []() {
        uint8_t record[RECORD_SIZE];
        bool wrapped_pushed;
        uint8_t second_marker;
        uint8_t wrapped_marker;
        uint32_t wrapped_available;
        const uint8_t *wrapped_record;

        fillRecord(record, 1);
        StepRing_Push(&ring, record, RECORD_SIZE);
        fillRecord(record, 2);
        StepRing_Push(&ring, record, RECORD_SIZE);
        StepRing_Release(&ring, RECORD_SIZE);
        fillRecord(record, 3);

        wrapped_pushed = StepRing_Push(&ring, record, RECORD_SIZE);
        second_marker = peekMarker();
        StepRing_Release(&ring, RECORD_SIZE);
        wrapped_available = StepRing_Peek(&ring, &wrapped_record);
        wrapped_marker = wrapped_record[0];

        expect(wrapped_pushed).toBe(true);
        expect(second_marker).toBe(2);
        expect(wrapped_marker).toBe(3);
        expect(wrapped_record == storage).toBe(true);
        expect(wrapped_available).toBe((uint32_t)RECORD_SIZE);
    });

    it("refuses a wrapped record that would overrun the oldest one", []() {
        uint8_t record[RECORD_SIZE];
        bool overrun_pushed;

        fillRecord(record, 1);
        StepRing_Push(&ring, record, RECORD_SIZE);
        StepRing_Push(&ring, record, RECORD_SIZE);
        StepRing_Release(&ring, RECORD_SIZE);
        StepRing_Push(&ring, record, RECORD_SIZE);

        overrun_pushed = StepRing_Push(&ring, record, RECORD_SIZE);

        expect(overrun_pushed).toBe(false);
    });

    it("starts over from the beginning once drained", []() {
        uint8_t record[RECORD_SIZE];
        const uint8_t *peeked;

        fillRecord(record, 1);
        StepRing_Push(&ring, record, RECORD_SIZE);
        StepRing_Release(&ring, RECORD_SIZE);
        fillRecord(record, 2);
        StepRing_Push(&ring, record, RECORD_SIZE);

        StepRing_Peek(&ring, &peeked);

        expect(peeked == storage).toBe(true);
        expect(peeked[0]).toBe(2);
    });
});

static void fillRecord(uint8_t *record, uint8_t marker)
{
    memset(record, marker, RECORD_SIZE);
}

static uint8_t peekMarker(void)
{
    const uint8_t *record;

    StepRing_Peek(&ring, &record);
    return record[0];
}
