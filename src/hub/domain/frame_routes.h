#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hub/domain/client_session.h"
#include "hub/domain/interface_registry.h"
#include "hub/domain/peer_directory.h"

/*
 * Bindings, not peers: several clients can open the same interface and each
 * holds up to CLIENT_SESSION_BINDINGS_MAX channels, so one frame from an agent
 * can owe a delivery to every binding in the hub. Sizing this by peer count
 * dropped every subscriber past the 64th with no error and no counter.
 */
#define FRAME_ROUTES_MAX (PEER_DIRECTORY_MAX * CLIENT_SESSION_BINDINGS_MAX)

/*
 * Pure routing service: given a frame source (peer + connection-scoped
 * channel), computes every destination it must be forwarded to, with the
 * channel translated to each destination's connection. No I/O, no state.
 */
typedef struct {
    uint32_t peer_id;
    uint8_t channel;
    bool suppress_echo;
    bool reliable;
} FrameRoute;

uint16_t FrameRoutes_FromAgent(
    const InterfaceRegistry *registry,
    PeerDirectory *directory,
    uint32_t agent_peer_id,
    uint8_t agent_channel,
    FrameRoute *routes,
    uint16_t routes_max,
    uint16_t *dropped
);
uint16_t FrameRoutes_FromClient(
    const InterfaceRegistry *registry,
    const HubPeer *client_peer,
    uint8_t client_channel,
    FrameRoute *routes,
    uint16_t routes_max,
    uint16_t *dropped
);
