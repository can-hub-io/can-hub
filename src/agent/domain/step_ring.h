#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Byte ring holding the transaction's step records verbatim, in arrival order,
 * over caller-owned storage. A record never splits across the end: when it
 * does not fit there, writing wraps to the start and the tail is skipped.
 * Callers size their credit to capacity minus one maximal record, which
 * covers that skipped tail.
 */
typedef struct {
    uint8_t *storage;
    uint32_t capacity;
    uint32_t read_offset;
    uint32_t write_offset;
    uint32_t wrap_offset;
    uint32_t used_bytes;
    bool wrapped;
} StepRing;

void StepRing_Init(StepRing *self, uint8_t *storage, uint32_t capacity);
void StepRing_Clear(StepRing *self);
bool StepRing_Push(StepRing *self, const uint8_t *record, uint32_t size);
uint32_t StepRing_Peek(const StepRing *self, const uint8_t **record);
void StepRing_Release(StepRing *self, uint32_t size);
bool StepRing_IsEmpty(const StepRing *self);
