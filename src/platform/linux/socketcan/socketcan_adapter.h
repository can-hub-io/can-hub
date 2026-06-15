#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "agent/ports/can_port.h"
#include "protocol/frame_message.h"
#include "protocol/register_message.h"

/*
 * SocketCAN adapter: one non-blocking CAN_RAW socket per exported interface,
 * CAN FD enabled, enlarged receive buffer (5000 fps bursts overflow the
 * default, see spike E0). Implements CanPort for the agent core; the platform
 * loop polls Fd(i) and drains with ReadFrame until it returns false.
 */
typedef struct {
    CanPort port;
    int32_t can_fds[REGISTER_INTERFACES_MAX];
    char interface_names[REGISTER_INTERFACES_MAX][REGISTER_INTERFACE_NAME_SIZE];
    uint8_t interface_count;
    uint32_t pace_rate_override;
} SocketCanAdapter;

bool SocketCanAdapter_Open(SocketCanAdapter *self, const RegisterMessage *registration, bool receive_own_messages);

/*
 * Force the bitrate the adapter advertises for pacing, overriding the kernel's
 * nominal bitrate. Needed on vcan, which has no bit timing; 0 leaves the kernel
 * value in effect.
 */
void SocketCanAdapter_SetPaceRate(SocketCanAdapter *self, uint32_t pace_rate);
void SocketCanAdapter_Close(SocketCanAdapter *self);
CanPort *SocketCanAdapter_Port(SocketCanAdapter *self);
int32_t SocketCanAdapter_Fd(const SocketCanAdapter *self, uint8_t interface_index);
bool SocketCanAdapter_ReadFrame(SocketCanAdapter *self, uint8_t interface_index, FrameMessage *frame);
