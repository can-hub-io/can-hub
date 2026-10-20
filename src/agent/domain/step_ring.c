#include "agent/domain/step_ring.h"

#include <string.h>

static bool pushContiguous(StepRing *self, const uint8_t *record, uint32_t size);
static bool pushWrapped(StepRing *self, const uint8_t *record, uint32_t size);
static void append(StepRing *self, const uint8_t *record, uint32_t size);
static void wrapReadAtEndOfFirstSegment(StepRing *self);

/* ---------- public ---------- */

void StepRing_Init(StepRing *self, uint8_t *storage, uint32_t capacity)
{
    self->storage = storage;
    self->capacity = capacity;
    StepRing_Clear(self);
}

void StepRing_Clear(StepRing *self)
{
    self->read_offset = 0;
    self->write_offset = 0;
    self->wrap_offset = 0;
    self->used_bytes = 0;
    self->wrapped = false;
}

bool StepRing_Push(StepRing *self, const uint8_t *record, uint32_t size)
{
    if (self->wrapped) {
        return pushWrapped(self, record, size);
    }
    return pushContiguous(self, record, size);
}

uint32_t StepRing_Peek(const StepRing *self, const uint8_t **record)
{
    uint32_t segment_end = self->wrapped ? self->wrap_offset : self->write_offset;

    if (self->used_bytes == 0) {
        return 0;
    }

    *record = self->storage + self->read_offset;

    return segment_end - self->read_offset;
}

void StepRing_Release(StepRing *self, uint32_t size)
{
    self->read_offset += size;
    self->used_bytes -= size;

    if (self->used_bytes == 0) {
        StepRing_Clear(self);
        return;
    }

    wrapReadAtEndOfFirstSegment(self);
}

bool StepRing_IsEmpty(const StepRing *self)
{
    return self->used_bytes == 0;
}

/* ---------- private ---------- */

static bool pushContiguous(StepRing *self, const uint8_t *record, uint32_t size)
{
    if (self->write_offset + size <= self->capacity) {
        append(self, record, size);
        return true;
    }
    if (size > self->read_offset) {
        return false;
    }

    self->wrap_offset = self->write_offset;
    self->write_offset = 0;
    self->wrapped = true;
    append(self, record, size);

    return true;
}

static bool pushWrapped(StepRing *self, const uint8_t *record, uint32_t size)
{
    if (self->write_offset + size > self->read_offset) {
        return false;
    }

    append(self, record, size);

    return true;
}

static void append(StepRing *self, const uint8_t *record, uint32_t size)
{
    memcpy(self->storage + self->write_offset, record, size);
    self->write_offset += size;
    self->used_bytes += size;
}

static void wrapReadAtEndOfFirstSegment(StepRing *self)
{
    if (!self->wrapped || self->read_offset != self->wrap_offset) {
        return;
    }

    self->read_offset = 0;
    self->wrapped = false;
}
