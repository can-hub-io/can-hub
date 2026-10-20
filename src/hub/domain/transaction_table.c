#include "hub/domain/transaction_table.h"

#include <string.h>

static bool isInUse(const RelayedTransaction *transaction);
static bool holdsLease(const RelayedTransaction *transaction);

/* ---------- public ---------- */

void TransactionTable_Reset(TransactionTable *self)
{
    memset(self, 0, sizeof(*self));
}

RelayedTransaction *TransactionTable_Open(TransactionTable *self, const RelayedTransaction *request)
{
    uint16_t i;

    if (TransactionTable_ForAgent(self, request->agent_peer_id) != NULL) {
        return NULL;
    }

    for(i=0; i<TRANSACTION_TABLE_MAX; i++) {
        if (!isInUse(&self->entries[i])) {
            self->entries[i] = *request;
            self->entries[i].state = kTRANSACTION_RELAY_STATE_PENDING;
            return &self->entries[i];
        }
    }

    return NULL;
}

void TransactionTable_Activate(TransactionTable *self, RelayedTransaction *transaction, uint16_t txn_id)
{
    transaction->state = kTRANSACTION_RELAY_STATE_ACTIVE;
    transaction->txn_id = txn_id;
    if (transaction->exclusive) {
        self->active_leases++;
    }
}

void TransactionTable_Close(TransactionTable *self, RelayedTransaction *transaction)
{
    if (holdsLease(transaction)) {
        self->active_leases--;
    }
    memset(transaction, 0, sizeof(*transaction));
}

RelayedTransaction *TransactionTable_ForAgent(TransactionTable *self, uint32_t agent_peer_id)
{
    uint16_t i;

    for(i=0; i<TRANSACTION_TABLE_MAX; i++) {
        if (isInUse(&self->entries[i]) && self->entries[i].agent_peer_id == agent_peer_id) {
            return &self->entries[i];
        }
    }

    return NULL;
}

RelayedTransaction *TransactionTable_ForClientChannel(TransactionTable *self, uint32_t client_peer_id, uint8_t client_channel)
{
    RelayedTransaction *transaction;
    uint16_t iterator = 0;

    while ((transaction = TransactionTable_NextForClient(self, client_peer_id, &iterator)) != NULL) {
        if (transaction->client_channel == client_channel) {
            return transaction;
        }
    }

    return NULL;
}

RelayedTransaction *TransactionTable_NextForClient(TransactionTable *self, uint32_t client_peer_id, uint16_t *iterator)
{
    RelayedTransaction *transaction;

    while (*iterator < TRANSACTION_TABLE_MAX) {
        transaction = &self->entries[*iterator];
        (*iterator)++;
        if (isInUse(transaction) && transaction->client_peer_id == client_peer_id) {
            return transaction;
        }
    }

    return NULL;
}

bool TransactionTable_AnyLease(const TransactionTable *self)
{
    return self->active_leases > 0;
}

bool TransactionTable_LeaseHolder(const TransactionTable *self, uint32_t interface_id, uint32_t *client_peer_id)
{
    const RelayedTransaction *transaction;
    uint16_t i;

    if (!TransactionTable_AnyLease(self)) {
        return false;
    }

    for(i=0; i<TRANSACTION_TABLE_MAX; i++) {
        transaction = &self->entries[i];
        if (holdsLease(transaction) && transaction->interface_id == interface_id) {
            *client_peer_id = transaction->client_peer_id;
            return true;
        }
    }

    return false;
}

/* ---------- private ---------- */

static bool isInUse(const RelayedTransaction *transaction)
{
    return transaction->state != kTRANSACTION_RELAY_STATE_FREE;
}

static bool holdsLease(const RelayedTransaction *transaction)
{
    return transaction->state == kTRANSACTION_RELAY_STATE_ACTIVE && transaction->exclusive;
}
