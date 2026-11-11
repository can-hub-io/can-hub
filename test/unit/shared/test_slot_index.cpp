#include <cest>

extern "C" {
#include "shared/slot_index.h"
}

#define BUCKET_COUNT 8
#define COLLIDING_KEYS 6

static SlotIndexBucket buckets[BUCKET_COUNT];
static SlotIndex slot_index;

describe("slot_index", []() {
    beforeEach([]() {
        SlotIndex_Init(&slot_index, buckets, BUCKET_COUNT);
    });

    it("finds nothing in an empty index", []() {
        uint16_t slot = 0;
        bool found;

        found = SlotIndex_Find(&slot_index, 42, &slot);

        expect(found).toBe(false);
    });

    it("finds the slot a key was put at", []() {
        uint16_t slot = 0;
        bool found;

        SlotIndex_Put(&slot_index, 0x80000001, 0);
        SlotIndex_Put(&slot_index, 0xC0000001, 7);

        found = SlotIndex_Find(&slot_index, 0xC0000001, &slot);

        expect(found).toBe(true);
        expect(slot).toBe((uint16_t)7);
    });

    it("replaces the slot of a key put twice", []() {
        uint16_t slot = 0;

        SlotIndex_Put(&slot_index, 42, 1);
        SlotIndex_Put(&slot_index, 42, 2);

        SlotIndex_Find(&slot_index, 42, &slot);

        expect(slot).toBe((uint16_t)2);
    });

    it("forgets a removed key", []() {
        uint16_t slot = 0;
        bool found;

        SlotIndex_Put(&slot_index, 42, 1);
        SlotIndex_Remove(&slot_index, 42);

        found = SlotIndex_Find(&slot_index, 42, &slot);

        expect(found).toBe(false);
    });

    it("keeps every other key reachable after removing one of a crowded index", []() {
        uint16_t slot = 0;
        uint16_t found_count = 0;
        uint64_t key;

        for(key=1; key<=COLLIDING_KEYS; key++) {
            SlotIndex_Put(&slot_index, key, (uint16_t)key);
        }

        for(key=1; key<=COLLIDING_KEYS; key++) {
            SlotIndex_Remove(&slot_index, key);
            SlotIndex_Put(&slot_index, key + COLLIDING_KEYS, (uint16_t)(key + COLLIDING_KEYS));
        }
        for(key=COLLIDING_KEYS+1; key<=2*COLLIDING_KEYS; key++) {
            if (SlotIndex_Find(&slot_index, key, &slot) && slot == key) {
                found_count++;
            }
        }

        expect(found_count).toBe((uint16_t)COLLIDING_KEYS);
    });

    it("refuses a new key once every bucket is taken", []() {
        uint64_t key;
        bool put;

        for(key=1; key<=BUCKET_COUNT; key++) {
            SlotIndex_Put(&slot_index, key, (uint16_t)key);
        }

        put = SlotIndex_Put(&slot_index, BUCKET_COUNT + 1, 0);

        expect(put).toBe(false);
    });
});
