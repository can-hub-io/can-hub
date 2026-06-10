#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "protocol/frame_message.h"
#include "protocol/message_header.h"

#define EGRESS_QUEUE_SLOTS_MAX 512
#define EGRESS_QUEUE_CHANNEL_CAP 64
#define EGRESS_QUEUE_CHANNELS_MAX 256
#define EGRESS_FRAME_BYTES_MAX (MESSAGE_HEADER_SIZE + FRAME_FIXED_FIELDS_SIZE + FRAME_PAYLOAD_MAX_FD)

/*
 * Per-peer pool of pending egress frames, shared by every channel the peer
 * holds open. When the transport refuses a frame (its backlog is full), the
 * broker parks it here and drains round-robin when the peer becomes writable
 * again, so a busy channel never starves a quiet one sharing the same peer.
 */
typedef enum tegress_push_result_e {
    kEGRESS_PUSH_QUEUED = 0,
    kEGRESS_PUSH_EVICTED_SELF,
    kEGRESS_PUSH_EVICTED_OTHER,
    kEGRESS_PUSH_RESULT_MAX,
} TEGRESS_PUSH_RESULT;

typedef struct {
    uint8_t data[EGRESS_FRAME_BYTES_MAX];
    uint16_t size;
    uint8_t channel;
    uint16_t next;
} EgressSlot;

typedef struct {
    uint16_t head;
    uint16_t tail;
    uint16_t count;
} EgressChannelList;

typedef struct {
    EgressSlot slots[EGRESS_QUEUE_SLOTS_MAX];
    EgressChannelList channels[EGRESS_QUEUE_CHANNELS_MAX];
    uint16_t free_head;
    uint16_t used;
    uint16_t cursor;
} EgressQueue;

void EgressQueue_Reset(EgressQueue *self);
TEGRESS_PUSH_RESULT EgressQueue_Push(
    EgressQueue *self,
    uint8_t channel,
    const uint8_t *data,
    uint16_t size,
    uint8_t *evicted_channel
);
bool EgressQueue_HasPending(const EgressQueue *self);
bool EgressQueue_ChannelPending(const EgressQueue *self, uint8_t channel);
const uint8_t *EgressQueue_FrontOfChannel(const EgressQueue *self, uint8_t channel, uint16_t *size);
void EgressQueue_PopChannel(EgressQueue *self, uint8_t channel);
bool EgressQueue_NextPendingChannel(EgressQueue *self, uint8_t *channel);
