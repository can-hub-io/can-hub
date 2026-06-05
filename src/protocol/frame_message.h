#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FRAME_PAYLOAD_MAX_CLASSIC 8
#define FRAME_PAYLOAD_MAX_FD 64
#define FRAME_FIXED_FIELDS_SIZE 16

#define FRAME_CAN_ID_MASK 0x1FFFFFFF
#define FRAME_CAN_ID_FLAG_ERR (1u << 29)
#define FRAME_CAN_ID_FLAG_RTR (1u << 30)
#define FRAME_CAN_ID_FLAG_EFF (1u << 31)

#define FRAME_FLAG_FD (1u << 0)
#define FRAME_FLAG_BRS (1u << 1)

typedef struct {
    uint32_t can_id;
    uint64_t timestamp_us;
    uint8_t channel;
    uint8_t payload_length;
    uint8_t frame_flags;
    uint8_t payload[FRAME_PAYLOAD_MAX_FD];
} FrameMessage;

size_t FrameMessage_Encode(const FrameMessage *self, uint8_t *buffer, size_t buffer_size);
bool FrameMessage_Decode(FrameMessage *self, const uint8_t *payload, size_t payload_length);
