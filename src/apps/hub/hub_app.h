#pragma once

#include "application/broker.h"
#include "ports/hub_transport_events.h"
#include "ports/hub_transport_port.h"

/*
 * Abstract assembly of the hub application: owns the broker and hands out
 * the inbound multi-peer event contract for the platform to plug into its
 * server transport. Freestanding.
 */
typedef struct {
    Broker broker;
} HubApp;

void HubApp_Init(HubApp *self, HubTransportPort *transport);
HubTransportEvents HubApp_Events(HubApp *self);
