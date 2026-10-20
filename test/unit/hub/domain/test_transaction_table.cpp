#include <cest>
#include <cstring>

extern "C" {
#include "hub/domain/transaction_table.h"
}

#define AGENT_PEER_ID 0x80000001
#define OTHER_AGENT_PEER_ID 0x80000002
#define CLIENT_PEER_ID 0x80000003
#define OTHER_CLIENT_PEER_ID 0x80000004
#define INTERFACE_ID 5
#define CLIENT_CHANNEL 2
#define TXN_ID 7

static TransactionTable table;

static RelayedTransaction request(uint32_t agent_peer_id, uint32_t client_peer_id, bool exclusive);

describe("transaction_table", []() {
    beforeEach([]() {
        TransactionTable_Reset(&table);
    });

    it("opens a pending transaction found by agent and by client channel", []() {
        RelayedTransaction wanted;
        RelayedTransaction *opened;
        RelayedTransaction *by_agent;
        RelayedTransaction *by_client;

        wanted = request(AGENT_PEER_ID, CLIENT_PEER_ID, false);

        opened = TransactionTable_Open(&table, &wanted);
        by_agent = TransactionTable_ForAgent(&table, AGENT_PEER_ID);
        by_client = TransactionTable_ForClientChannel(&table, CLIENT_PEER_ID, CLIENT_CHANNEL);

        expect(opened->state).toBe((uint8_t)kTRANSACTION_RELAY_STATE_PENDING);
        expect(by_agent == opened).toBe(true);
        expect(by_client == opened).toBe(true);
    });

    it("refuses a second transaction on the same agent", []() {
        RelayedTransaction wanted;
        RelayedTransaction *second;

        wanted = request(AGENT_PEER_ID, CLIENT_PEER_ID, false);
        TransactionTable_Open(&table, &wanted);
        wanted.client_peer_id = OTHER_CLIENT_PEER_ID;

        second = TransactionTable_Open(&table, &wanted);

        expect(second == NULL).toBe(true);
    });

    it("leases the interface only while an exclusive transaction is active", []() {
        RelayedTransaction wanted;
        RelayedTransaction *opened;
        uint32_t holder = 0;
        bool leased_while_pending;
        bool leased_while_active;
        bool leased_after_close;

        wanted = request(AGENT_PEER_ID, CLIENT_PEER_ID, true);
        opened = TransactionTable_Open(&table, &wanted);

        leased_while_pending = TransactionTable_LeaseHolder(&table, INTERFACE_ID, &holder);
        TransactionTable_Activate(&table, opened, TXN_ID);
        leased_while_active = TransactionTable_LeaseHolder(&table, INTERFACE_ID, &holder);
        TransactionTable_Close(&table, opened);
        leased_after_close = TransactionTable_AnyLease(&table);

        expect(leased_while_pending).toBe(false);
        expect(leased_while_active).toBe(true);
        expect(holder).toBe((uint32_t)CLIENT_PEER_ID);
        expect(leased_after_close).toBe(false);
    });

    it("does not lease for a shared transaction", []() {
        RelayedTransaction wanted;
        RelayedTransaction *opened;

        wanted = request(AGENT_PEER_ID, CLIENT_PEER_ID, false);
        opened = TransactionTable_Open(&table, &wanted);

        TransactionTable_Activate(&table, opened, TXN_ID);

        expect(TransactionTable_AnyLease(&table)).toBe(false);
    });

    it("walks every transaction of one client", []() {
        RelayedTransaction wanted;
        uint16_t iterator = 0;
        uint8_t found = 0;

        wanted = request(AGENT_PEER_ID, CLIENT_PEER_ID, false);
        TransactionTable_Open(&table, &wanted);
        wanted.agent_peer_id = OTHER_AGENT_PEER_ID;
        TransactionTable_Open(&table, &wanted);
        wanted = request(OTHER_AGENT_PEER_ID + 1, OTHER_CLIENT_PEER_ID, false);
        TransactionTable_Open(&table, &wanted);

        while (TransactionTable_NextForClient(&table, CLIENT_PEER_ID, &iterator) != NULL) {
            found++;
        }

        expect(found).toBe(2);
    });
});

static RelayedTransaction request(uint32_t agent_peer_id, uint32_t client_peer_id, bool exclusive)
{
    RelayedTransaction wanted;

    memset(&wanted, 0, sizeof(wanted));
    wanted.exclusive = exclusive;
    wanted.interface_id = INTERFACE_ID;
    wanted.agent_peer_id = agent_peer_id;
    wanted.client_peer_id = client_peer_id;
    wanted.client_channel = CLIENT_CHANNEL;
    return wanted;
}
