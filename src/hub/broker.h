#pragma once

#include "hub/domain/interface_registry.h"
#include "hub/domain/peer_directory.h"
#include "hub/ports/hub_transport_events.h"
#include "hub/ports/identity_store_port.h"
#include "hub/ports/hub_transport_port.h"

/*
 * Broker: mediates between agents and clients. Admits peers, keeps the
 * interface catalogue through the domain, answers the client control plane
 * and forwards frames along the routes the domain computes. Thin
 * orchestration — every rule lives in domain/.
 */
typedef struct {
    uint64_t frames_received;
    uint64_t frames_forwarded;
    uint64_t frames_dropped;
    uint64_t frames_unroutable;
} HubMetrics;

typedef struct {
    HubTransportPort *transport;
    IdentityStorePort *identity_store;
    InterfaceRegistry registry;
    PeerDirectory directory;
    HubMetrics metrics;
} Broker;

void Broker_Init(Broker *self, HubTransportPort *transport, IdentityStorePort *identity_store);
HubTransportEvents Broker_Events(Broker *self);
void Broker_Tick(Broker *self, uint64_t now_us);
