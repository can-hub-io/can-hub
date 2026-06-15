#pragma once

#include "hub/broker.h"
#include "hub/ports/hub_transport_events.h"
#include "hub/ports/hub_transport_port.h"

/*
 * Abstract assembly of the hub application: owns the broker and hands out
 * the inbound multi-peer event contract for the platform to plug into its
 * server transport. Freestanding.
 */
typedef struct {
    Broker broker;
} HubApp;

void HubApp_Init(
    HubApp *self,
    HubTransportPort *transport,
    IdentityStorePort *identity_store,
    AuthorizationPort *authorization,
    bool require_known_agents
);
HubTransportEvents HubApp_Events(HubApp *self);
void HubApp_Tick(HubApp *self, uint64_t now_us);
int32_t HubApp_NextTimeoutMs(HubApp *self, int32_t cap_ms);
