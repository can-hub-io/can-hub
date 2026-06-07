#include <cest>

#include <cstring>

extern "C" {
#include "hub/domain/admin_views.h"
}

#define TRUCK_PEER 100
#define VAN_PEER 101
#define CLIENT_PEER 200
#define IDLE_CLIENT_PEER 201

static InterfaceRegistry registry;
static PeerDirectory directory;
static AdminAgentsReplyMessage agents_reply;
static AdminClientsReplyMessage clients_reply;
static const RegisterMessage truck_registration = { "truck42", 2, { "can0", "can1" } };
static const RegisterMessage van_registration = { "van7", 1, { "can2" } };

static void addAgent(uint32_t peer_id, const RegisterMessage *registration);
static HubPeer *addClient(uint32_t peer_id);
static uint8_t openInterfaceAt(HubPeer *client, uint8_t registry_index);

describe("admin_views", []() {
    beforeEach([]() {
        InterfaceRegistry_Reset(&registry);
        PeerDirectory_Reset(&directory);
        addAgent(TRUCK_PEER, &truck_registration);
        addAgent(VAN_PEER, &van_registration);
    });

    it("lists agents with their interface count", []() {
        AdminViews_Agents(&registry, &directory, "", 0, &agents_reply);

        expect(agents_reply.count).toBe(2);
        expect(agents_reply.entries[0].peer_id).toBe((uint32_t)TRUCK_PEER);
        expect(agents_reply.entries[0].interface_count).toBe(2);
        expect((const char *)agents_reply.entries[0].agent_name).toBe("truck42");
        expect(agents_reply.entries[1].interface_count).toBe(1);
        expect((const char *)agents_reply.entries[1].agent_name).toBe("van7");
    });

    it("filters agents by name", []() {
        AdminViews_Agents(&registry, &directory, "van7", 0, &agents_reply);

        expect(agents_reply.count).toBe(1);
        expect((const char *)agents_reply.entries[0].agent_name).toBe("van7");
    });

    it("returns no agents for an unknown filter", []() {
        AdminViews_Agents(&registry, &directory, "ghost", 0, &agents_reply);

        expect(agents_reply.count).toBe(0);
    });

    it("lists open client channels resolved to names", []() {
        HubPeer *client = addClient(CLIENT_PEER);
        uint8_t truck_channel = openInterfaceAt(client, 0);
        uint8_t van_channel = openInterfaceAt(client, 2);

        addClient(IDLE_CLIENT_PEER);

        AdminViews_Clients(&registry, &directory, "", 0, &clients_reply);

        expect(clients_reply.count).toBe(3);
        expect(clients_reply.entries[0].peer_id).toBe((uint32_t)CLIENT_PEER);
        expect(clients_reply.entries[0].channel).toBe(truck_channel);
        expect((const char *)clients_reply.entries[0].agent_name).toBe("truck42");
        expect((const char *)clients_reply.entries[0].interface_name).toBe("can0");
        expect(clients_reply.entries[1].channel).toBe(van_channel);
        expect((const char *)clients_reply.entries[1].agent_name).toBe("van7");
        expect(clients_reply.entries[2].peer_id).toBe((uint32_t)IDLE_CLIENT_PEER);
        expect(clients_reply.entries[2].channel).toBe(ADMIN_CLIENT_NO_CHANNEL);
        expect((const char *)clients_reply.entries[2].agent_name).toBe("");
    });

    it("filters client channels by agent and drops idle clients", []() {
        HubPeer *client = addClient(CLIENT_PEER);

        openInterfaceAt(client, 0);
        openInterfaceAt(client, 2);
        addClient(IDLE_CLIENT_PEER);

        AdminViews_Clients(&registry, &directory, "truck42", 0, &clients_reply);

        expect(clients_reply.count).toBe(1);
        expect(clients_reply.entries[0].peer_id).toBe((uint32_t)CLIENT_PEER);
        expect((const char *)clients_reply.entries[0].agent_name).toBe("truck42");
        expect((const char *)clients_reply.entries[0].interface_name).toBe("can0");
    });

    it("lists interfaces with subscribers and traffic", []() {
        AdminInterfacesReplyMessage reply;
        HubPeer *client = addClient(CLIENT_PEER);

        openInterfaceAt(client, 0);
        InterfaceRegistry_CountFrame(&registry, registry.entries[0].interface_id);
        InterfaceRegistry_CountFrame(&registry, registry.entries[0].interface_id);

        AdminViews_Interfaces(&registry, &directory, 0, &reply);

        expect(reply.count).toBe(3);
        expect(reply.entries[0].interface_id).toBe(registry.entries[0].interface_id);
        expect(reply.entries[0].subscriber_count).toBe(1);
        expect(reply.entries[0].frames_received).toBe((uint64_t)2);
        expect((const char *)reply.entries[0].agent_name).toBe("truck42");
        expect((const char *)reply.entries[0].interface_name).toBe("can0");
        expect(reply.entries[1].subscriber_count).toBe(0);
        expect(reply.entries[1].frames_received).toBe((uint64_t)0);
        expect((const char *)reply.entries[2].agent_name).toBe("van7");
    });

    it("flags more and honours the offset when rows exceed one reply", []() {
        uint8_t i;

        for(i=0; i<ADMIN_CLIENTS_REPLY_ENTRIES_MAX + 1; i++) {
            addClient(CLIENT_PEER + i);
        }

        AdminViews_Clients(&registry, &directory, "", 0, &clients_reply);
        expect(clients_reply.count).toBe(ADMIN_CLIENTS_REPLY_ENTRIES_MAX);
        expect(clients_reply.flags).toBe(ADMIN_REPLY_FLAG_MORE);

        AdminViews_Clients(&registry, &directory, "", ADMIN_CLIENTS_REPLY_ENTRIES_MAX, &clients_reply);
        expect(clients_reply.count).toBe(1);
        expect(clients_reply.flags).toBe(0);
        expect(clients_reply.entries[0].peer_id).toBe((uint32_t)(CLIENT_PEER + ADMIN_CLIENTS_REPLY_ENTRIES_MAX));
    });
});

/* ---------- private ---------- */

static void addAgent(uint32_t peer_id, const RegisterMessage *registration)
{
    RegisterAckMessage ack;
    HubPeer *peer = PeerDirectory_Allocate(&directory, peer_id);

    peer->role = kHUB_PEER_ROLE_AGENT;
    HubPeer_SetAgentName(peer, registration->agent_name);
    InterfaceRegistry_RegisterAgent(&registry, peer_id, registration, &ack);
}

static HubPeer *addClient(uint32_t peer_id)
{
    HubPeer *peer = PeerDirectory_Allocate(&directory, peer_id);

    peer->role = kHUB_PEER_ROLE_CLIENT;

    return peer;
}

static uint8_t openInterfaceAt(HubPeer *client, uint8_t registry_index)
{
    uint8_t channel = 0;

    ClientSession_OpenInterface(&client->session, registry.entries[registry_index].interface_id, &channel);

    return channel;
}
