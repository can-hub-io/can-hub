#pragma once

#include <stdbool.h>

#include "protocol/frame_message.h"

/*
 * Outbound CAN access for the agent core: inject a frame into the local bus
 * identified by interface_index. Inbound frames are pushed into the core by
 * the platform loop via Agent_OnCanFrame — they do not travel through this
 * port. Freestanding: implementations may be SocketCAN, bxCAN registers, ...
 */
typedef struct {
    void *context;
    bool (*write_frame)(void *context, uint8_t interface_index, const FrameMessage *frame);
} CanPort;
