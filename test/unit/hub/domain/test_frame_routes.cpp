#include <cest>

extern "C" {
#include "hub/domain/frame_routes.h"
}

#define FIRST_CLIENT_PEER 200
#define CLIENTS_PAST_PEER_COUNT (PEER_DIRECTORY_MAX / CLIENT_SESSION_BINDINGS_MAX + 1)

static InterfaceRegistry registry;
static PeerDirectory directory;
static SubscriberIndex subscribers;
static const RegisterMessage truck_registration = { "truck42", 2, { "can0", "can1" } };

static void subscribeOpenBindings(void);

describe("frame_routes", []() {
    beforeEach([]() {
        RegisterAckMessage ack;
        HubPeer *agent;
        HubPeer *client;

        InterfaceRegistry_Reset(&registry);
        PeerDirectory_Reset(&directory);
        SubscriberIndex_Reset(&subscribers);
        agent = PeerDirectory_Allocate(&directory, 100);
        agent->role = kHUB_PEER_ROLE_AGENT;
        client = PeerDirectory_Allocate(&directory, 200);
        client->role = kHUB_PEER_ROLE_CLIENT;
        InterfaceRegistry_RegisterAgent(&registry, 100, &truck_registration, &ack);
    });

    it("routes an agent frame to every client with the interface open", []() {
        HubPeer *client = PeerDirectory_Find(&directory, 200);
        const InterfaceEntry *entry = InterfaceRegistry_FindByAgentChannel(&registry, 100, 1);
        ChannelOpenRequest request = {
            .interface_id = entry->interface_id,
            .suppress_echo = false,
            .can_write = false,
            .reliable = false,
        };
        FrameRoute routes[FRAME_ROUTES_MAX];
        uint8_t client_channel = 0;
        uint16_t route_count;
        uint16_t routes_dropped;

        ClientSession_OpenInterface(&client->session, &request, &client_channel);

        subscribeOpenBindings();
        route_count = FrameRoutes_FromAgent(&registry, &subscribers, &directory, 100, 1, routes, FRAME_ROUTES_MAX, &routes_dropped);

        expect(route_count).toBe(1);
        expect(routes[0].peer_id).toBe((uint32_t)200);
        expect(routes[0].channel).toBe(client_channel);
    });

    it("marks the route reliable when the destination binding is reliable", []() {
        HubPeer *client = PeerDirectory_Find(&directory, 200);
        const InterfaceEntry *entry = InterfaceRegistry_FindByAgentChannel(&registry, 100, 1);
        ChannelOpenRequest request = {
            .interface_id = entry->interface_id,
            .suppress_echo = false,
            .can_write = false,
            .reliable = true,
        };
        FrameRoute routes[FRAME_ROUTES_MAX];
        uint8_t client_channel = 0;
        uint16_t route_count;
        uint16_t routes_dropped;

        ClientSession_OpenInterface(&client->session, &request, &client_channel);

        subscribeOpenBindings();
        route_count = FrameRoutes_FromAgent(&registry, &subscribers, &directory, 100, 1, routes, FRAME_ROUTES_MAX, &routes_dropped);

        expect(route_count).toBe(1);
        expect(routes[0].reliable).toBe(true);
    });

    it("routes an agent frame to every binding of a client that opened the interface twice", []() {
        HubPeer *client = PeerDirectory_Find(&directory, 200);
        const InterfaceEntry *entry = InterfaceRegistry_FindByAgentChannel(&registry, 100, 1);
        ChannelOpenRequest request = {
            .interface_id = entry->interface_id,
            .suppress_echo = false,
            .can_write = false,
            .reliable = false,
        };
        FrameRoute routes[FRAME_ROUTES_MAX];
        uint8_t first_channel = 0;
        uint8_t second_channel = 0;
        uint16_t route_count;
        uint16_t routes_dropped;

        ClientSession_OpenInterface(&client->session, &request, &first_channel);
        ClientSession_OpenInterface(&client->session, &request, &second_channel);

        subscribeOpenBindings();
        route_count = FrameRoutes_FromAgent(&registry, &subscribers, &directory, 100, 1, routes, FRAME_ROUTES_MAX, &routes_dropped);

        expect(route_count).toBe(2);
        expect(routes[0].peer_id).toBe((uint32_t)200);
        expect(routes[0].channel).toBe(first_channel);
        expect(routes[1].peer_id).toBe((uint32_t)200);
        expect(routes[1].channel).toBe(second_channel);
    });

    it("caps routes at the buffer capacity across bindings", []() {
        HubPeer *client = PeerDirectory_Find(&directory, 200);
        const InterfaceEntry *entry = InterfaceRegistry_FindByAgentChannel(&registry, 100, 1);
        ChannelOpenRequest request = {
            .interface_id = entry->interface_id,
            .suppress_echo = false,
            .can_write = false,
            .reliable = false,
        };
        FrameRoute routes[FRAME_ROUTES_MAX];
        uint8_t channel = 0;
        uint16_t route_count;
        uint16_t routes_dropped;

        ClientSession_OpenInterface(&client->session, &request, &channel);
        ClientSession_OpenInterface(&client->session, &request, &channel);

        subscribeOpenBindings();
        route_count = FrameRoutes_FromAgent(&registry, &subscribers, &directory, 100, 1, routes, 1, &routes_dropped);

        expect(route_count).toBe(1);
        expect(routes_dropped).toBe(1);
    });

    it("routes past the old peer-count ceiling", []() {
        FrameRoute routes[FRAME_ROUTES_MAX];
        const InterfaceEntry *entry = InterfaceRegistry_FindByAgentChannel(&registry, 100, 1);
        ChannelOpenRequest request = {
            .interface_id = entry->interface_id,
            .suppress_echo = false,
            .can_write = false,
            .reliable = false,
        };
        HubPeer *client;
        uint8_t channel;
        uint16_t route_count;
        uint16_t routes_dropped;
        uint16_t expected = 0;
        uint32_t peer_id;
        uint8_t opened;

        for(peer_id=FIRST_CLIENT_PEER; peer_id<FIRST_CLIENT_PEER + CLIENTS_PAST_PEER_COUNT; peer_id++) {
            client = PeerDirectory_Allocate(&directory, peer_id);
            client->role = kHUB_PEER_ROLE_CLIENT;
            for(opened=0; opened<CLIENT_SESSION_BINDINGS_MAX; opened++) {
                channel = 0;
                ClientSession_OpenInterface(&client->session, &request, &channel);
                expected++;
            }
        }

        subscribeOpenBindings();
        route_count = FrameRoutes_FromAgent(&registry, &subscribers, &directory, 100, 1, routes, FRAME_ROUTES_MAX, &routes_dropped);

        expect(expected > PEER_DIRECTORY_MAX).toBe(true);
        expect(route_count).toBe(expected);
        expect(routes_dropped).toBe(0);
    });

    it("routes nothing when no client opened the interface", []() {
        FrameRoute routes[FRAME_ROUTES_MAX];
        uint16_t route_count;
        uint16_t routes_dropped;

        subscribeOpenBindings();
        route_count = FrameRoutes_FromAgent(&registry, &subscribers, &directory, 100, 1, routes, FRAME_ROUTES_MAX, &routes_dropped);

        expect(route_count).toBe(0);
    });

    it("routes nothing for an unknown agent channel", []() {
        FrameRoute routes[FRAME_ROUTES_MAX];
        uint16_t route_count;
        uint16_t routes_dropped;

        subscribeOpenBindings();
        route_count = FrameRoutes_FromAgent(&registry, &subscribers, &directory, 100, 42, routes, FRAME_ROUTES_MAX, &routes_dropped);

        expect(route_count).toBe(0);
    });

    it("routes a client frame to the owning agent with its channel", []() {
        HubPeer *client = PeerDirectory_Find(&directory, 200);
        const InterfaceEntry *entry = InterfaceRegistry_FindByAgentChannel(&registry, 100, 1);
        ChannelOpenRequest request = {
            .interface_id = entry->interface_id,
            .suppress_echo = false,
            .can_write = false,
            .reliable = false,
        };
        FrameRoute routes[FRAME_ROUTES_MAX];
        uint8_t client_channel = 0;
        uint16_t route_count;
        uint16_t routes_dropped;

        ClientSession_OpenInterface(&client->session, &request, &client_channel);

        route_count = FrameRoutes_FromClient(&registry, client, client_channel, routes, FRAME_ROUTES_MAX, &routes_dropped);

        expect(route_count).toBe(1);
        expect(routes[0].peer_id).toBe((uint32_t)100);
        expect(routes[0].channel).toBe(1);
    });

    it("routes nothing for a client channel that is not open", []() {
        HubPeer *client = PeerDirectory_Find(&directory, 200);
        FrameRoute routes[FRAME_ROUTES_MAX];
        uint16_t route_count;
        uint16_t routes_dropped;

        route_count = FrameRoutes_FromClient(&registry, client, 5, routes, FRAME_ROUTES_MAX, &routes_dropped);

        expect(route_count).toBe(0);
    });
});

static void subscribeOpenBindings(void)
{
    const ChannelBinding *binding;
    const InterfaceEntry *entry;
    HubPeer *peer;
    uint16_t peer_slot;
    uint8_t binding_index;

    for(peer_slot=0; peer_slot<PEER_DIRECTORY_MAX; peer_slot++) {
        peer = PeerDirectory_At(&directory, peer_slot);
        for(binding_index=0; peer!=NULL && binding_index<CLIENT_SESSION_BINDINGS_MAX; binding_index++) {
            binding = ClientSession_BindingAt(&peer->session, binding_index);
            if (binding == NULL) {
                continue;
            }
            entry = InterfaceRegistry_FindById(&registry, binding->interface_id);
            SubscriberIndex_Subscribe(&subscribers, InterfaceRegistry_SlotOf(&registry, entry), peer_slot, binding_index);
        }
    }
}
