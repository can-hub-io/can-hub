#include "hub/domain/frame_routes.h"

static const ChannelBinding *boundTo(const HubPeer *peer, uint8_t binding_index, uint32_t interface_id);

/* ---------- public ---------- */

uint16_t FrameRoutes_FromAgent(
    const InterfaceRegistry *registry,
    const SubscriberIndex *subscribers,
    PeerDirectory *directory,
    uint32_t agent_peer_id,
    uint8_t agent_channel,
    FrameRoute *routes,
    uint16_t routes_max,
    uint16_t *dropped
)
{
    const InterfaceEntry *entry = InterfaceRegistry_FindByAgentChannel(registry, agent_peer_id, agent_channel);

    *dropped = 0;
    if (entry == NULL) {
        return 0;
    }

    return FrameRoutes_ToInterface(registry, subscribers, directory, entry, routes, routes_max, dropped);
}

uint16_t FrameRoutes_ToInterface(
    const InterfaceRegistry *registry,
    const SubscriberIndex *subscribers,
    PeerDirectory *directory,
    const InterfaceEntry *entry,
    FrameRoute *routes,
    uint16_t routes_max,
    uint16_t *dropped
)
{
    const ChannelBinding *binding;
    HubPeer *peer;
    uint16_t route_count = 0;
    uint16_t subscriber;
    bool pending;

    *dropped = 0;

    pending = SubscriberIndex_First(subscribers, InterfaceRegistry_SlotOf(registry, entry), &subscriber);
    while (pending) {
        peer = PeerDirectory_At(directory, SubscriberIndex_PeerSlot(subscriber));
        binding = boundTo(peer, SubscriberIndex_BindingIndex(subscriber), entry->interface_id);
        pending = SubscriberIndex_Next(subscribers, &subscriber);
        if (binding == NULL) {
            continue;
        }
        if (route_count == routes_max) {
            (*dropped)++;
            continue;
        }

        routes[route_count].peer_id = peer->peer_id;
        routes[route_count].channel = binding->channel;
        routes[route_count].suppress_echo = binding->suppress_echo;
        routes[route_count].reliable = binding->reliable;
        route_count++;
    }

    return route_count;
}

uint16_t FrameRoutes_FromClient(
    const InterfaceRegistry *registry,
    const HubPeer *client_peer,
    uint8_t client_channel,
    FrameRoute *routes,
    uint16_t routes_max,
    uint16_t *dropped
)
{
    const InterfaceEntry *entry;
    uint32_t interface_id;

    *dropped = 0;

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
    routes[0].suppress_echo = false;
    routes[0].reliable = ClientSession_ChannelReliable(&client_peer->session, client_channel);

    return 1;
}

/* ---------- private ---------- */

static const ChannelBinding *boundTo(const HubPeer *peer, uint8_t binding_index, uint32_t interface_id)
{
    const ChannelBinding *binding;

    if (peer == NULL || peer->role != kHUB_PEER_ROLE_CLIENT) {
        return NULL;
    }

    binding = ClientSession_BindingAt(&peer->session, binding_index);
    if (binding == NULL || binding->interface_id != interface_id) {
        return NULL;
    }

    return binding;
}
