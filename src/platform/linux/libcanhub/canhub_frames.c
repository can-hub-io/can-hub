#include "platform/linux/libcanhub/canhub_frames.h"

#include <string.h>

#include "client/can_id.h"

#define WIRE_FRAME_FLAGS (CANHUB_FRAME_FLAG_FD | CANHUB_FRAME_FLAG_BRS)

static void fromWireFields(uint32_t can_id, uint8_t frame_flags, CanHubFrame *frame);

/* ---------- public ---------- */

void CanHubFrames_FromWire(const FrameMessage *message, CanHubFrame *frame)
{
    memset(frame, 0, sizeof(*frame));
    fromWireFields(message->can_id, message->frame_flags, frame);
    frame->timestamp_us = message->timestamp_us;
    if (message->route_flags & FRAME_ROUTE_FLAG_BRIDGED) {
        frame->flags |= CANHUB_FRAME_FLAG_BRIDGED;
    }
    frame->length = message->payload_length;
    memcpy(frame->payload, message->payload, message->payload_length);
}

void CanHubFrames_FromRxFrame(const TxnRxFrame *rx_frame, CanHubFrame *frame)
{
    memset(frame, 0, sizeof(*frame));
    fromWireFields(rx_frame->can_id, rx_frame->frame_flags, frame);
    frame->length = rx_frame->payload_length;
    memcpy(frame->payload, rx_frame->payload, rx_frame->payload_length);
}

bool CanHubFrames_ToWire(const CanHubFrame *frame, FrameMessage *message)
{
    if (!CanId_IsValid(frame->id) || frame->length > CANHUB_FRAME_PAYLOAD_MAX) {
        return false;
    }
    if (frame->length > FRAME_PAYLOAD_MAX_CLASSIC && (frame->flags & CANHUB_FRAME_FLAG_FD) == 0) {
        return false;
    }

    memset(message, 0, sizeof(*message));
    message->can_id = CanId_ToWire(frame->id);
    if (frame->flags & CANHUB_FRAME_FLAG_RTR) {
        message->can_id |= FRAME_CAN_ID_FLAG_RTR;
    }
    message->timestamp_us = frame->timestamp_us;
    message->frame_flags = frame->flags & WIRE_FRAME_FLAGS;
    message->payload_length = frame->length;
    memcpy(message->payload, frame->payload, frame->length);
    return true;
}

bool CanHubFrames_FilterToWire(const CanHubFilter *filter, CanFilter *wire)
{
    uint32_t compared = CanId_Width(filter->id) & ~filter->id_ignored_bits;

    if (!CanId_IsValid(filter->id) || !CanId_AreIgnoredBitsValid(filter->id, filter->id_ignored_bits)) {
        return false;
    }

    wire->can_id = filter->id.value & compared;
    if (filter->id.extended) {
        wire->can_id |= FRAME_CAN_ID_FLAG_EFF;
    }
    wire->can_mask = compared | FRAME_CAN_ID_FLAG_EFF;
    return true;
}

/* ---------- private ---------- */

static void fromWireFields(uint32_t can_id, uint8_t frame_flags, CanHubFrame *frame)
{
    frame->id.value = can_id & FRAME_CAN_ID_MASK;
    frame->id.extended = (can_id & FRAME_CAN_ID_FLAG_EFF) != 0;
    frame->flags = frame_flags & WIRE_FRAME_FLAGS;
    if (can_id & FRAME_CAN_ID_FLAG_RTR) {
        frame->flags |= CANHUB_FRAME_FLAG_RTR;
    }
    if (can_id & FRAME_CAN_ID_FLAG_ERR) {
        frame->flags |= CANHUB_FRAME_FLAG_ERROR;
    }
}
