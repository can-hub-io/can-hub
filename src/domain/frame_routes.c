#include "domain/frame_routes.h"

/* ---------- public ---------- */

uint8_t FrameRoutes_FromAgent(
    const InterfaceRegistry *registry,
    PeerDirectory *directory,
    uint32_t agent_peer_id,
    uint8_t agent_channel,
    FrameRoute *routes,
    uint8_t routes_max
)
{
    const InterfaceEntry *entry;
    HubPeer *peer;
    uint8_t route_count = 0;
    uint8_t peer_index;
    uint8_t client_channel;

    entry = InterfaceRegistry_FindByAgentChannel(registry, agent_peer_id, agent_channel);
    if (entry == NULL) {
        return 0;
    }

    for(peer_index=0; peer_index<PEER_DIRECTORY_MAX; peer_index++) {
        peer = PeerDirectory_At(directory, peer_index);
        if (peer == NULL || peer->role != kHUB_PEER_ROLE_CLIENT) {
            continue;
        }
        if (!ClientSession_ChannelForInterface(&peer->session, entry->interface_id, &client_channel)) {
            continue;
        }
        if (route_count == routes_max) {
            return route_count;
        }

        routes[route_count].peer_id = peer->peer_id;
        routes[route_count].channel = client_channel;
        route_count++;
    }

    return route_count;
}

uint8_t FrameRoutes_FromClient(
    const InterfaceRegistry *registry,
    const HubPeer *client_peer,
    uint8_t client_channel,
    FrameRoute *routes,
    uint8_t routes_max
)
{
    const InterfaceEntry *entry;
    uint32_t interface_id;

    if (routes_max == 0) {
        return 0;
    }
    if (!ClientSession_InterfaceForChannel(&client_peer->session, client_channel, &interface_id)) {
        return 0;
    }

    entry = InterfaceRegistry_FindById(registry, interface_id);
    if (entry == NULL) {
        return 0;
    }

    routes[0].peer_id = entry->agent_peer_id;
    routes[0].channel = entry->agent_channel;

    return 1;
}
