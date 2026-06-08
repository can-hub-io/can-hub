#pragma once

#include <stdbool.h>

#include "protocol/frame_message.h"

/*
 * Outbound CAN access for the agent core: inject a frame into the local bus
 * identified by interface_index. Inbound frames are pushed into the core by
 * the platform loop via Agent_OnCanFrame — they do not travel through this
 * port. Freestanding: implementations may be SocketCAN, bxCAN registers, ...
 *
 * configure applies an interface-level change (op: set bitrate, link up,
 * link down) requested by the hub administrator; returns false when the
 * interface index is unknown or the change could not be applied.
 */
typedef struct {
    void *context;
    bool (*write_frame)(void *context, uint8_t interface_index, const FrameMessage *frame);
    bool (*configure)(void *context, uint8_t interface_index, uint8_t op, uint32_t bitrate);
} CanPort;
