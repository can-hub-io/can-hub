#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "protocol/frame_message.h"

/*
 * Inbound CAN contract, defined by the core and implemented by it (see
 * Agent_CanEvents). The platform pushes every frame captured on a local
 * bus. Counterpart of CanPort, which carries the outbound direction.
 *
 * on_frame returns false when the reliable data plane could not accept the
 * frame (its send buffer is full): the platform must stop reading the origin
 * bus and retry this frame before reading more, so the kernel queue applies
 * backpressure to the source instead of the frame being dropped mid-stream.
 * true means accepted (or dropped for an unexported/malformed frame) — keep
 * reading.
 */
typedef struct {
    void *context;
    bool (*on_frame)(void *context, uint8_t interface_index, const FrameMessage *frame);
} CanEvents;
