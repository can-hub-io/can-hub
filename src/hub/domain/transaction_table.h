#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hub/domain/peer_directory.h"

#define TRANSACTION_TABLE_MAX PEER_DIRECTORY_MAX
#define TRANSACTION_RELAY_WINDOW_MAX (48 * 1024)
#define TRANSACTION_NO_CLIENT 0

/*
 * The transactions the hub relays, at most one per agent (the agent runs one
 * at a time). An entry is pending from the client's TXN_BEGIN until the
 * agent's TXN_BEGIN_ACK, then active until TXN_STATUS. An exclusive active
 * entry leases its interface to its client. A pending entry whose client left
 * keeps no client, so the hub can cancel the transaction once the agent
 * grants it.
 */
typedef enum ttransaction_relay_state_e {
    kTRANSACTION_RELAY_STATE_FREE = 0,
    kTRANSACTION_RELAY_STATE_PENDING = 1,
    kTRANSACTION_RELAY_STATE_ACTIVE = 2,
    kTRANSACTION_RELAY_STATE_MAX,
} TTRANSACTION_RELAY_STATE;

typedef struct {
    uint8_t state;
    bool exclusive;
    uint16_t txn_id;
    uint32_t interface_id;
    uint32_t agent_peer_id;
    uint8_t agent_channel;
    uint32_t client_peer_id;
    uint8_t client_channel;
    uint64_t started_at_us;
    uint64_t last_progress_us;
    uint32_t completed_steps;
    uint32_t relayed_bytes;
    uint32_t consumed_bytes;
} RelayedTransaction;

typedef struct {
    RelayedTransaction entries[TRANSACTION_TABLE_MAX];
    uint16_t active_leases;
} TransactionTable;

void TransactionTable_Reset(TransactionTable *self);
RelayedTransaction *TransactionTable_Open(TransactionTable *self, const RelayedTransaction *request);
void TransactionTable_Activate(TransactionTable *self, RelayedTransaction *transaction, uint16_t txn_id);
void TransactionTable_Close(TransactionTable *self, RelayedTransaction *transaction);
RelayedTransaction *TransactionTable_ForAgent(TransactionTable *self, uint32_t agent_peer_id);
RelayedTransaction *TransactionTable_ForClientChannel(TransactionTable *self, uint32_t client_peer_id, uint8_t client_channel);
RelayedTransaction *TransactionTable_NextForClient(TransactionTable *self, uint32_t client_peer_id, uint16_t *iterator);
bool TransactionTable_AnyLease(const TransactionTable *self);
bool TransactionTable_LeaseHolder(const TransactionTable *self, uint32_t interface_id, uint32_t *client_peer_id);
