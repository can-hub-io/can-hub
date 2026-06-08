#pragma once

#include "hub/domain/interface_registry.h"
#include "hub/domain/peer_directory.h"
#include "hub/ports/authorization_port.h"
#include "hub/ports/hub_transport_events.h"
#include "hub/ports/identity_store_port.h"
#include "hub/ports/hub_transport_port.h"
#include "protocol/register_message.h"

#define BROKER_PENDING_IFCONFIG_MAX 16

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
    bool in_use;
    uint32_t admin_peer_id;
    uint32_t agent_peer_id;
    char interface_name[REGISTER_INTERFACE_NAME_SIZE];
} PendingIfconfig;

typedef struct {
    HubTransportPort *transport;
    IdentityStorePort *identity_store;
    AuthorizationPort *authorization;
    InterfaceRegistry registry;
    PeerDirectory directory;
    HubMetrics metrics;
    PendingIfconfig pending_ifconfig[BROKER_PENDING_IFCONFIG_MAX];
    bool require_known_agents;
} Broker;

void Broker_Init(
    Broker *self,
    HubTransportPort *transport,
    IdentityStorePort *identity_store,
    AuthorizationPort *authorization,
    bool require_known_agents
);
HubTransportEvents Broker_Events(Broker *self);
void Broker_Tick(Broker *self, uint64_t now_us);
