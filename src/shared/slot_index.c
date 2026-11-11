#include "shared/slot_index.h"

#include <string.h>

#define HASH_MULTIPLIER 0x9E3779B97F4A7C15
#define HASH_SHIFT 32
#define NO_OCCUPANT 0

static uint16_t homeOf(const SlotIndex *self, uint64_t key);
static uint16_t following(const SlotIndex *self, uint16_t bucket);
static uint16_t distance(const SlotIndex *self, uint16_t from, uint16_t to);
static bool locate(const SlotIndex *self, uint64_t key, uint16_t *bucket);
static void pullDisplacedEntriesIntoGap(SlotIndex *self, uint16_t gap);

/* ---------- public ---------- */

void SlotIndex_Init(SlotIndex *self, SlotIndexBucket *buckets, uint16_t bucket_count)
{
    self->buckets = buckets;
    self->bucket_count = bucket_count;
    memset(buckets, 0, (size_t)bucket_count * sizeof(*buckets));
}

bool SlotIndex_Put(SlotIndex *self, uint64_t key, uint16_t slot)
{
    uint16_t bucket = homeOf(self, key);
    uint16_t probed;

    for(probed=0; probed<self->bucket_count; probed++) {
        if (self->buckets[bucket].occupant == NO_OCCUPANT || self->buckets[bucket].key == key) {
            self->buckets[bucket].key = key;
            self->buckets[bucket].occupant = (uint16_t)(slot + 1);
            return true;
        }
        bucket = following(self, bucket);
    }

    return false;
}

bool SlotIndex_Find(const SlotIndex *self, uint64_t key, uint16_t *slot)
{
    uint16_t bucket;

    if (!locate(self, key, &bucket)) {
        return false;
    }

    *slot = (uint16_t)(self->buckets[bucket].occupant - 1);

    return true;
}

void SlotIndex_Remove(SlotIndex *self, uint64_t key)
{
    uint16_t bucket;

    if (locate(self, key, &bucket)) {
        pullDisplacedEntriesIntoGap(self, bucket);
    }
}

/* ---------- private ---------- */

static uint16_t homeOf(const SlotIndex *self, uint64_t key)
{
    uint64_t mixed = (key * HASH_MULTIPLIER) >> HASH_SHIFT;

    return (uint16_t)(mixed & (uint64_t)(self->bucket_count - 1));
}

static uint16_t following(const SlotIndex *self, uint16_t bucket)
{
    return (uint16_t)((bucket + 1) & (self->bucket_count - 1));
}

static uint16_t distance(const SlotIndex *self, uint16_t from, uint16_t to)
{
    return (uint16_t)((to - from) & (self->bucket_count - 1));
}

static bool locate(const SlotIndex *self, uint64_t key, uint16_t *bucket)
{
    uint16_t candidate = homeOf(self, key);
    uint16_t probed;

    for(probed=0; probed<self->bucket_count; probed++) {
        if (self->buckets[candidate].occupant == NO_OCCUPANT) {
            return false;
        }
        if (self->buckets[candidate].key == key) {
            *bucket = candidate;
            return true;
        }
        candidate = following(self, candidate);
    }

    return false;
}

static void pullDisplacedEntriesIntoGap(SlotIndex *self, uint16_t gap)
{
    uint16_t candidate = following(self, gap);
    uint16_t home;
    uint16_t probed;

    for(probed=1; probed<self->bucket_count; probed++) {
        if (self->buckets[candidate].occupant == NO_OCCUPANT) {
            break;
        }
        home = homeOf(self, self->buckets[candidate].key);
        if (distance(self, home, candidate) >= distance(self, gap, candidate)) {
            self->buckets[gap] = self->buckets[candidate];
            gap = candidate;
        }
        candidate = following(self, candidate);
    }

    self->buckets[gap].occupant = NO_OCCUPANT;
}
