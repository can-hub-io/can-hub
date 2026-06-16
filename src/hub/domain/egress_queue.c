#include "hub/domain/egress_queue.h"

#include <string.h>

#define EGRESS_SLOT_NIL 0xFFFF

static uint16_t popChannelHead(EgressQueue *self, uint8_t channel);
static void appendChannelTail(EgressQueue *self, uint8_t channel, uint16_t slot);
static uint8_t fullestChannel(const EgressQueue *self);
static void fillSlot(EgressSlot *slot, uint8_t channel, const uint8_t *data, uint16_t size, uint64_t now_us);

/* ---------- public ---------- */

void EgressQueue_Reset(EgressQueue *self)
{
    uint16_t i;

    memset(self, 0, sizeof(*self));
    for(i=0; i<EGRESS_QUEUE_SLOTS_MAX; i++) {
        self->slots[i].next = (uint16_t)(i + 1);
    }
    self->slots[EGRESS_QUEUE_SLOTS_MAX - 1].next = EGRESS_SLOT_NIL;
    self->free_head = 0;
    self->used = 0;
    self->cursor = 0;
    for(i=0; i<EGRESS_QUEUE_CHANNELS_MAX; i++) {
        self->channels[i].head = EGRESS_SLOT_NIL;
        self->channels[i].tail = EGRESS_SLOT_NIL;
        self->channels[i].count = 0;
    }
}

TEGRESS_PUSH_RESULT EgressQueue_Push(EgressQueue *self, uint8_t channel, const uint8_t *data, uint16_t size, uint64_t now_us, uint8_t *evicted_channel)
{
    uint8_t bully;
    uint16_t slot;

    if (size > EGRESS_FRAME_BYTES_MAX) {
        return kEGRESS_PUSH_QUEUED;
    }

    if (self->channels[channel].count >= EGRESS_QUEUE_CHANNEL_CAP) {
        slot = popChannelHead(self, channel);
        fillSlot(&self->slots[slot], channel, data, size, now_us);
        appendChannelTail(self, channel, slot);
        *evicted_channel = channel;
        return kEGRESS_PUSH_EVICTED_SELF;
    }

    if (self->free_head != EGRESS_SLOT_NIL) {
        slot = self->free_head;
        self->free_head = self->slots[slot].next;
        self->used++;
        fillSlot(&self->slots[slot], channel, data, size, now_us);
        appendChannelTail(self, channel, slot);
        return kEGRESS_PUSH_QUEUED;
    }

    bully = fullestChannel(self);
    slot = popChannelHead(self, bully);
    fillSlot(&self->slots[slot], channel, data, size, now_us);
    appendChannelTail(self, channel, slot);
    *evicted_channel = bully;
    return kEGRESS_PUSH_EVICTED_OTHER;
}

bool EgressQueue_HasPending(const EgressQueue *self)
{
    return self->used > 0;
}

bool EgressQueue_ChannelPending(const EgressQueue *self, uint8_t channel)
{
    return self->channels[channel].count > 0;
}

const uint8_t *EgressQueue_FrontOfChannel(const EgressQueue *self, uint8_t channel, uint16_t *size)
{
    uint16_t head = self->channels[channel].head;

    if (head == EGRESS_SLOT_NIL) {
        return NULL;
    }

    *size = self->slots[head].size;
    return self->slots[head].data;
}

uint64_t EgressQueue_FrontEnqueuedUs(const EgressQueue *self, uint8_t channel)
{
    uint16_t head = self->channels[channel].head;

    if (head == EGRESS_SLOT_NIL) {
        return 0;
    }

    return self->slots[head].enqueued_us;
}

void EgressQueue_PopChannel(EgressQueue *self, uint8_t channel)
{
    uint16_t slot;

    if (self->channels[channel].head == EGRESS_SLOT_NIL) {
        return;
    }

    slot = popChannelHead(self, channel);
    self->slots[slot].next = self->free_head;
    self->free_head = slot;
    self->used--;
}

bool EgressQueue_NextPendingChannel(EgressQueue *self, uint8_t *channel)
{
    uint16_t i;
    uint16_t candidate;

    for(i=0; i<EGRESS_QUEUE_CHANNELS_MAX; i++) {
        candidate = (uint16_t)((self->cursor + i) % EGRESS_QUEUE_CHANNELS_MAX);
        if (self->channels[candidate].count > 0) {
            self->cursor = (uint16_t)((candidate + 1) % EGRESS_QUEUE_CHANNELS_MAX);
            *channel = (uint8_t)candidate;
            return true;
        }
    }

    return false;
}

/* ---------- private ---------- */

static uint16_t popChannelHead(EgressQueue *self, uint8_t channel)
{
    uint16_t slot = self->channels[channel].head;

    self->channels[channel].head = self->slots[slot].next;
    if (self->channels[channel].head == EGRESS_SLOT_NIL) {
        self->channels[channel].tail = EGRESS_SLOT_NIL;
    }
    self->channels[channel].count--;

    return slot;
}

static void appendChannelTail(EgressQueue *self, uint8_t channel, uint16_t slot)
{
    self->slots[slot].next = EGRESS_SLOT_NIL;
    if (self->channels[channel].tail == EGRESS_SLOT_NIL) {
        self->channels[channel].head = slot;
    } else {
        self->slots[self->channels[channel].tail].next = slot;
    }
    self->channels[channel].tail = slot;
    self->channels[channel].count++;
}

static uint8_t fullestChannel(const EgressQueue *self)
{
    uint16_t i;
    uint16_t best = 0;
    uint16_t best_count = 0;

    for(i=0; i<EGRESS_QUEUE_CHANNELS_MAX; i++) {
        if (self->channels[i].count > best_count) {
            best_count = self->channels[i].count;
            best = i;
        }
    }

    return (uint8_t)best;
}

static void fillSlot(EgressSlot *slot, uint8_t channel, const uint8_t *data, uint16_t size, uint64_t now_us)
{
    slot->channel = channel;
    slot->size = size;
    slot->enqueued_us = now_us;
    memcpy(slot->data, data, size);
}
