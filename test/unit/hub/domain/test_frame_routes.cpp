#include <cest>

extern "C" {
#include "hub/domain/frame_routes.h"
}

static InterfaceRegistry registry;
static PeerDirectory directory;
static const RegisterMessage truck_registration = { "truck42", 2, { "can0", "can1" } };

describe("frame_routes", []() {
    beforeEach([]() {
        RegisterAckMessage ack;
        HubPeer *agent;
        HubPeer *client;

        InterfaceRegistry_Reset(&registry);
        PeerDirectory_Reset(&directory);
        agent = PeerDirectory_Allocate(&directory, 100);
        agent->role = kHUB_PEER_ROLE_AGENT;
        client = PeerDirectory_Allocate(&directory, 200);
        client->role = kHUB_PEER_ROLE_CLIENT;
        InterfaceRegistry_RegisterAgent(&registry, 100, &truck_registration, &ack);
    });

    it("routes an agent frame to every client with the interface open", []() {
        HubPeer *client = PeerDirectory_Find(&directory, 200);
        const InterfaceEntry *entry = InterfaceRegistry_FindByAgentChannel(&registry, 100, 1);
        FrameRoute routes[FRAME_ROUTES_MAX];
        uint8_t client_channel = 0;
        uint8_t route_count;

        ClientSession_OpenInterface(&client->session, entry->interface_id, false, false, false, &client_channel);

        route_count = FrameRoutes_FromAgent(&registry, &directory, 100, 1, routes, FRAME_ROUTES_MAX);

        expect(route_count).toBe(1);
        expect(routes[0].peer_id).toBe((uint32_t)200);
        expect(routes[0].channel).toBe(client_channel);
    });

    it("routes an agent frame to every binding of a client that opened the interface twice", []() {
        HubPeer *client = PeerDirectory_Find(&directory, 200);
        const InterfaceEntry *entry = InterfaceRegistry_FindByAgentChannel(&registry, 100, 1);
        FrameRoute routes[FRAME_ROUTES_MAX];
        uint8_t first_channel = 0;
        uint8_t second_channel = 0;
        uint8_t route_count;

        ClientSession_OpenInterface(&client->session, entry->interface_id, false, false, false, &first_channel);
        ClientSession_OpenInterface(&client->session, entry->interface_id, false, false, false, &second_channel);

        route_count = FrameRoutes_FromAgent(&registry, &directory, 100, 1, routes, FRAME_ROUTES_MAX);

        expect(route_count).toBe(2);
        expect(routes[0].peer_id).toBe((uint32_t)200);
        expect(routes[0].channel).toBe(first_channel);
        expect(routes[1].peer_id).toBe((uint32_t)200);
        expect(routes[1].channel).toBe(second_channel);
    });

    it("caps routes at the buffer capacity across bindings", []() {
        HubPeer *client = PeerDirectory_Find(&directory, 200);
        const InterfaceEntry *entry = InterfaceRegistry_FindByAgentChannel(&registry, 100, 1);
        FrameRoute routes[FRAME_ROUTES_MAX];
        uint8_t channel = 0;
        uint8_t route_count;

        ClientSession_OpenInterface(&client->session, entry->interface_id, false, false, false, &channel);
        ClientSession_OpenInterface(&client->session, entry->interface_id, false, false, false, &channel);

        route_count = FrameRoutes_FromAgent(&registry, &directory, 100, 1, routes, 1);

        expect(route_count).toBe(1);
    });

    it("routes nothing when no client opened the interface", []() {
        FrameRoute routes[FRAME_ROUTES_MAX];
        uint8_t route_count;

        route_count = FrameRoutes_FromAgent(&registry, &directory, 100, 1, routes, FRAME_ROUTES_MAX);

        expect(route_count).toBe(0);
    });

    it("routes nothing for an unknown agent channel", []() {
        FrameRoute routes[FRAME_ROUTES_MAX];
        uint8_t route_count;

        route_count = FrameRoutes_FromAgent(&registry, &directory, 100, 42, routes, FRAME_ROUTES_MAX);

        expect(route_count).toBe(0);
    });

    it("routes a client frame to the owning agent with its channel", []() {
        HubPeer *client = PeerDirectory_Find(&directory, 200);
        const InterfaceEntry *entry = InterfaceRegistry_FindByAgentChannel(&registry, 100, 1);
        FrameRoute routes[FRAME_ROUTES_MAX];
        uint8_t client_channel = 0;
        uint8_t route_count;

        ClientSession_OpenInterface(&client->session, entry->interface_id, false, false, false, &client_channel);

        route_count = FrameRoutes_FromClient(&registry, client, client_channel, routes, FRAME_ROUTES_MAX);

        expect(route_count).toBe(1);
        expect(routes[0].peer_id).toBe((uint32_t)100);
        expect(routes[0].channel).toBe(1);
    });

    it("routes nothing for a client channel that is not open", []() {
        HubPeer *client = PeerDirectory_Find(&directory, 200);
        FrameRoute routes[FRAME_ROUTES_MAX];
        uint8_t route_count;

        route_count = FrameRoutes_FromClient(&registry, client, 5, routes, FRAME_ROUTES_MAX);

        expect(route_count).toBe(0);
    });
});
