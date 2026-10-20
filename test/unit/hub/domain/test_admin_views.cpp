#include <cest>

#include <cstring>

extern "C" {
#include "hub/domain/admin_views.h"
}

#define TRUCK_PEER 100
#define VAN_PEER 101
#define CLIENT_PEER 200
#define IDLE_CLIENT_PEER 201
#define TXN_ID 42
#define STARTED_AT_US 1000

static InterfaceRegistry registry;
static PeerDirectory directory;
static AdminAgentsReplyMessage agents_reply;
static AdminClientsReplyMessage clients_reply;
static TransactionTable transactions;
static AdminTxnsReplyMessage transactions_reply;
static const RegisterMessage truck_registration = { "truck42", 2, { "can0", "can1" } };
static const RegisterMessage van_registration = { "van7", 1, { "can2" } };

static void addAgent(uint32_t peer_id, const RegisterMessage *registration);
static RelayedTransaction *relayTransaction(uint32_t agent_peer_id, uint32_t interface_id, bool exclusive);
static HubPeer *addClient(uint32_t peer_id);
static void addClients(uint8_t count);
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

    it("flags more when rows exceed one reply", []() {
        addClients(ADMIN_CLIENTS_REPLY_ENTRIES_MAX + 1);

        AdminViews_Clients(&registry, &directory, "", 0, &clients_reply);

        expect(clients_reply.count).toBe(ADMIN_CLIENTS_REPLY_ENTRIES_MAX);
        expect(clients_reply.flags).toBe(ADMIN_REPLY_FLAG_MORE);
    });

    it("honours the offset of the next page", []() {
        addClients(ADMIN_CLIENTS_REPLY_ENTRIES_MAX + 1);

        AdminViews_Clients(&registry, &directory, "", ADMIN_CLIENTS_REPLY_ENTRIES_MAX, &clients_reply);

        expect(clients_reply.count).toBe(1);
        expect(clients_reply.flags).toBe(0);
        expect(clients_reply.entries[0].peer_id).toBe((uint32_t)(CLIENT_PEER + ADMIN_CLIENTS_REPLY_ENTRIES_MAX));
    });
});

describe("admin_views transactions", []() {
    beforeEach([]() {
        InterfaceRegistry_Reset(&registry);
        PeerDirectory_Reset(&directory);
        TransactionTable_Reset(&transactions);
        addAgent(TRUCK_PEER, &truck_registration);
    });

    it("shows a relayed transaction with its bus, owner, lease and credit in flight", []() {
        RelayedTransaction *transaction;
        AdminTxnsReplyEntry *entry;

        transaction = relayTransaction(TRUCK_PEER, registry.entries[0].interface_id, true);
        TransactionTable_Activate(&transactions, transaction, TXN_ID);
        transaction->completed_steps = 25;
        transaction->relayed_bytes = 1000;
        transaction->consumed_bytes = 400;

        AdminViews_Transactions(&registry, &transactions, STARTED_AT_US + 2500000, 0, &transactions_reply);
        entry = &transactions_reply.entries[0];

        expect(transactions_reply.count).toBe(1);
        expect((const char *)entry->agent_name).toBe("truck42");
        expect((const char *)entry->interface_name).toBe("can0");
        expect(entry->client_peer_id).toBe((uint32_t)CLIENT_PEER);
        expect(entry->state).toBe((uint8_t)ADMIN_TXN_STATE_ACTIVE);
        expect(entry->flags).toBe((uint8_t)ADMIN_TXN_FLAG_EXCLUSIVE);
        expect(entry->txn_id).toBe((uint16_t)TXN_ID);
        expect(entry->completed_steps).toBe((uint32_t)25);
        expect(entry->bytes_in_flight).toBe((uint32_t)600);
        expect(entry->age_ms).toBe((uint32_t)2500);
        expect(entry->progress_age_ms).toBe((uint32_t)ADMIN_TXN_NO_PROGRESS);
    });

    it("shows a transaction still waiting for the agent's grant", []() {
        RelayedTransaction *transaction;

        transaction = relayTransaction(TRUCK_PEER, registry.entries[0].interface_id, false);
        transaction->last_progress_us = STARTED_AT_US + 1000000;

        AdminViews_Transactions(&registry, &transactions, STARTED_AT_US + 3000000, 0, &transactions_reply);

        expect(transactions_reply.entries[0].state).toBe((uint8_t)ADMIN_TXN_STATE_PENDING);
        expect(transactions_reply.entries[0].flags).toBe(0);
        expect(transactions_reply.entries[0].progress_age_ms).toBe((uint32_t)2000);
    });

    it("pages after a full reply", []() {
        uint8_t first_count;
        uint8_t first_flags;
        uint8_t i;

        for(i=0; i<ADMIN_TXNS_REPLY_ENTRIES_MAX + 1; i++) {
            relayTransaction(TRUCK_PEER + 1 + i, registry.entries[0].interface_id, false);
        }

        AdminViews_Transactions(&registry, &transactions, STARTED_AT_US, 0, &transactions_reply);
        first_count = transactions_reply.count;
        first_flags = transactions_reply.flags;
        AdminViews_Transactions(&registry, &transactions, STARTED_AT_US, ADMIN_TXNS_REPLY_ENTRIES_MAX, &transactions_reply);

        expect(first_count).toBe(ADMIN_TXNS_REPLY_ENTRIES_MAX);
        expect(first_flags).toBe(ADMIN_REPLY_FLAG_MORE);
        expect(transactions_reply.count).toBe(1);
        expect(transactions_reply.flags).toBe(0);
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
    ChannelOpenRequest request = {
        .interface_id = registry.entries[registry_index].interface_id,
        .suppress_echo = false,
        .can_write = false,
        .reliable = false,
    };
    uint8_t channel = 0;

    ClientSession_OpenInterface(&client->session, &request, &channel);

    return channel;
}

static void addClients(uint8_t count)
{
    uint8_t i;

    for(i=0; i<count; i++) {
        addClient(CLIENT_PEER + i);
    }
}

static RelayedTransaction *relayTransaction(uint32_t agent_peer_id, uint32_t interface_id, bool exclusive)
{
    RelayedTransaction request;

    memset(&request, 0, sizeof(request));
    request.exclusive = exclusive;
    request.interface_id = interface_id;
    request.agent_peer_id = agent_peer_id;
    request.client_peer_id = CLIENT_PEER;
    request.started_at_us = STARTED_AT_US;
    return TransactionTable_Open(&transactions, &request);
}
