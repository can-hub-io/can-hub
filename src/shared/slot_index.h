#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Open-addressing hash from a 64-bit key to the slot of a fixed table, so a
 * lookup does not walk the table. The owner provides the buckets: a power of
 * two, at least twice the slots it indexes. Zeroed buckets are an empty index.
 */
typedef struct {
    uint64_t key;
    uint16_t occupant;
} SlotIndexBucket;

typedef struct {
    SlotIndexBucket *buckets;
    uint16_t bucket_count;
} SlotIndex;

void SlotIndex_Init(SlotIndex *self, SlotIndexBucket *buckets, uint16_t bucket_count);
bool SlotIndex_Put(SlotIndex *self, uint64_t key, uint16_t slot);
bool SlotIndex_Find(const SlotIndex *self, uint64_t key, uint16_t *slot);
void SlotIndex_Remove(SlotIndex *self, uint64_t key);
