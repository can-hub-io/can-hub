#pragma once

#include <stdint.h>

#include "application/agent.h"
#include "ports/can_events.h"
#include "ports/can_port.h"
#include "ports/transport_events.h"
#include "ports/transport_port.h"
#include "protocol/register_message.h"

/*
 * Abstract assembly of the agent application: owns the core and hands out
 * the inbound event contracts for the platform to plug into its adapters.
 * Freestanding — every platform entry point (Linux epoll loop, firmware
 * superloop) drives one of these.
 */
typedef struct {
    Agent agent;
} AgentApp;

void AgentApp_Init(AgentApp *self, TransportPort *transport, CanPort *can, const RegisterMessage *registration);
TransportEvents AgentApp_TransportEvents(AgentApp *self);
CanEvents AgentApp_CanEvents(AgentApp *self);
void AgentApp_Tick(AgentApp *self, uint64_t now_us);
