#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "domain/channel_map.h"
#include "domain/reconnect_backoff.h"
#include "ports/can_port.h"
#include "ports/transport_port.h"
#include "protocol/frame_message.h"
#include "protocol/register_message.h"

/*
 * Agent core: freestanding state machine, no POSIX, no heap. The platform
 * loop pushes events in (Agent_On*) and injects time (Agent_Tick); ports
 * carry everything that leaves the core.
 */

typedef enum tagent_state_e {
    kAGENT_STATE_DISCONNECTED = 0,
    kAGENT_STATE_CONNECTING,
    kAGENT_STATE_REGISTERING,
    kAGENT_STATE_RUNNING,
    kAGENT_STATE_MAX,
} TAGENT_STATE;

typedef struct {
    TransportPort *transport;
    CanPort *can;
    RegisterMessage registration;
    ChannelMap channel_map;
    ReconnectBackoff backoff;
    uint8_t state;
    uint64_t next_connect_at_us;
} Agent;

void Agent_Init(Agent *self, TransportPort *transport, CanPort *can, const RegisterMessage *registration);
void Agent_Tick(Agent *self, uint64_t now_us);
void Agent_OnConnected(Agent *self);
void Agent_OnDisconnected(Agent *self, uint64_t now_us);
void Agent_OnControlMessage(Agent *self, const uint8_t *data, size_t size, uint64_t now_us);
void Agent_OnCanFrame(Agent *self, uint8_t interface_index, const FrameMessage *frame);
void Agent_OnTransportFrame(Agent *self, const uint8_t *data, size_t size);
uint8_t Agent_State(const Agent *self);
