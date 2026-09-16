#pragma once

#include "hub/domain/interface_registry.h"
#include "protocol/frame_message.h"
#include "hub/domain/frame_routes.h"
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
/*
 * Tokens are carried in the 6 bits of route_flags above the two flag bits, and
 * 0 means none, so slots 0..62 are addressable and slot 63 is not. That is one
 * short of PEER_DIRECTORY_MAX by construction: a peer in the last slot simply
 * gets no token and its injections are never echo-suppressed.
 */
#define FRAME_ROUTE_TOKEN_VALUES_MAX ((FRAME_ROUTE_TOKEN_MASK >> FRAME_ROUTE_TOKEN_SHIFT))

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
    /* 16 KB, so not on the stack of onPeerFrame. Shared safely only while
       nothing reachable from a send re-enters that function. */
    FrameRoute frame_routes[FRAME_ROUTES_MAX];
    /* Which peer each injection token was issued to. A slot is reused when its
       peer disconnects, so the token alone cannot say who injected. */
    uint32_t injection_token_owner[FRAME_ROUTE_TOKEN_VALUES_MAX];
    bool require_known_agents;
    uint64_t now_us;
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

/*
 * Milliseconds until the next paced frame becomes drainable, clamped to
 * cap_ms. The platform loop uses it as the poll timeout so paced egress wakes
 * on time instead of waiting a full poll period; returns cap_ms when nothing is
 * waiting on shaper credit.
 */
int32_t Broker_NextTimeoutMs(Broker *self, int32_t cap_ms);
