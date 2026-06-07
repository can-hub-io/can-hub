#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hub/domain/interface_registry.h"
#include "hub/domain/peer_directory.h"

#define FRAME_ROUTES_MAX PEER_DIRECTORY_MAX

/*
 * Pure routing service: given a frame source (peer + connection-scoped
 * channel), computes every destination it must be forwarded to, with the
 * channel translated to each destination's connection. No I/O, no state.
 */
typedef struct {
    uint32_t peer_id;
    uint8_t channel;
    bool suppress_echo;
} FrameRoute;

uint8_t FrameRoutes_FromAgent(
    const InterfaceRegistry *registry,
    PeerDirectory *directory,
    uint32_t agent_peer_id,
    uint8_t agent_channel,
    FrameRoute *routes,
    uint8_t routes_max
);
uint8_t FrameRoutes_FromClient(
    const InterfaceRegistry *registry,
    const HubPeer *client_peer,
    uint8_t client_channel,
    FrameRoute *routes,
    uint8_t routes_max
);
