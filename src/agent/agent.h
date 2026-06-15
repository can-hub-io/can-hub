#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "agent/domain/channel_map.h"
#include "agent/domain/echo_correlator.h"
#include "agent/domain/reconnect_backoff.h"
#include "agent/domain/tx_pacer.h"
#include "agent/ports/can_events.h"
#include "agent/ports/can_port.h"
#include "agent/ports/transport_events.h"
#include "agent/ports/transport_port.h"
#include "protocol/frame_message.h"
#include "protocol/register_message.h"

/*
 * Agent core: freestanding state machine, no POSIX, no heap. The platform
 * loop pushes events in (Agent_On*) and injects time (Agent_Tick); ports
 * carry everything that leaves the core.
 */

#define AGENT_REGISTER_TIMEOUT_MS 5000
#define AGENT_STATUS_PERIOD_MS 1000

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
    EchoCorrelator echo;
    TxPacer tx_pacer;
    uint8_t state;
    uint64_t next_connect_at_us;
    uint64_t register_deadline_us;
    uint64_t next_status_at_us;
    uint64_t tx_dropped[REGISTER_INTERFACES_MAX];
    uint32_t pending_reconnect_delay_ms;
} Agent;

void Agent_Init(Agent *self, TransportPort *transport, CanPort *can, const RegisterMessage *registration);
TransportEvents Agent_TransportEvents(Agent *self);
CanEvents Agent_CanEvents(Agent *self);
void Agent_Tick(Agent *self, uint64_t now_us);
void Agent_OnConnected(Agent *self);
void Agent_OnDisconnected(Agent *self, uint64_t now_us);
void Agent_OnControlMessage(Agent *self, const uint8_t *data, size_t size, uint64_t now_us);
void Agent_OnCanFrame(Agent *self, uint8_t interface_index, const FrameMessage *frame);
void Agent_OnTransportFrame(Agent *self, const uint8_t *data, size_t size);
uint8_t Agent_State(const Agent *self);
uint32_t Agent_PendingReconnectDelayMs(const Agent *self);
