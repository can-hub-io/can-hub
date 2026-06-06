#pragma once

#include <stdint.h>

#include "protocol/frame_message.h"

/*
 * Inbound CAN contract, defined by the core and implemented by it (see
 * Agent_CanEvents). The platform pushes every frame captured on a local
 * bus. Counterpart of CanPort, which carries the outbound direction.
 */
typedef struct {
    void *context;
    void (*on_frame)(void *context, uint8_t interface_index, const FrameMessage *frame);
} CanEvents;
