#pragma once

#include <stdbool.h>

#include "protocol/frame_message.h"
#include "protocol/ifconfig_message.h"

/*
 * Outbound CAN access for the agent core: inject a frame into the local bus
 * identified by interface_index. Inbound frames are pushed into the core by
 * the platform loop via Agent_OnCanFrame — they do not travel through this
 * port. Freestanding: implementations may be SocketCAN, bxCAN registers, ...
 *
 * configure applies the settings whose fields are set, all at once, as
 * requested through the hub; on failure it returns false and writes why into
 * detail (IFCONFIG_DETAIL_SIZE bytes). query reads the settings the device
 * holds now; returns false when the interface index is unknown.
 *
 * bitrate reports the interface's nominal bus bitrate (bits/s) so the core can
 * advertise it for rate-matched pacing; returns 0 when unknown.
 */
typedef struct {
    void *context;
    bool (*write_frame)(void *context, uint8_t interface_index, const FrameMessage *frame);
    bool (*configure)(void *context, uint8_t interface_index, const InterfaceSettings *requested, char *detail);
    bool (*query)(void *context, uint8_t interface_index, InterfaceSettings *effective);
    uint32_t (*bitrate)(void *context, uint8_t interface_index);
} CanPort;
