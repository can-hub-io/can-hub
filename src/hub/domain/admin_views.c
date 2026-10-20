#include "hub/domain/admin_views.h"

#include <string.h>

#include "shared/bounded_string.h"

#define MICROSECONDS_PER_MILLISECOND 1000

static uint8_t interfaceMode(const InterfaceEntry *entry);
static uint8_t countAgentInterfaces(const InterfaceRegistry *registry, uint32_t agent_peer_id);
static uint8_t countSubscribers(const PeerDirectory *directory, uint32_t interface_id);
static bool clientHasOpenChannels(const HubPeer *peer);
static void fillTransactionEntry(
    const InterfaceRegistry *registry,
    const RelayedTransaction *transaction,
    uint64_t now_us,
    AdminTxnsReplyEntry *entry
);
static uint32_t elapsedMs(uint64_t since_us, uint64_t now_us);
static bool appendClientRow(
    AdminClientsReplyMessage *reply,
    uint16_t *skipped,
    uint16_t offset,
    uint32_t peer_id,
    uint32_t interface_id,
    uint8_t channel,
    const char *agent_name,
    const char *interface_name,
    uint32_t frames_forwarded,
    uint32_t frames_dropped
);

/* ---------- public ---------- */

void AdminViews_Agents(
    const InterfaceRegistry *registry,
    const PeerDirectory *directory,
    const char *agent_name_filter,
    uint16_t offset,
    AdminAgentsReplyMessage *reply
)
{
    const HubPeer *peer;
    AdminAgentsReplyEntry *entry;
    bool filtered = agent_name_filter[0] != '\0';
    uint16_t skipped = 0;
    uint16_t i;

    memset(reply, 0, sizeof(*reply));

    for(i=0; i<PEER_DIRECTORY_MAX; i++) {
        peer = &directory->peers[i];
        if (!peer->in_use || peer->role != kHUB_PEER_ROLE_AGENT) {
            continue;
        }
        if (filtered && strcmp(peer->agent_name, agent_name_filter) != 0) {
            continue;
        }
        if (skipped < offset) {
            skipped++;
            continue;
        }
        if (reply->count == ADMIN_AGENTS_REPLY_ENTRIES_MAX) {
            reply->flags |= ADMIN_REPLY_FLAG_MORE;
            return;
        }
        entry = &reply->entries[reply->count++];
        entry->peer_id = peer->peer_id;
        entry->interface_count = countAgentInterfaces(registry, peer->peer_id);
        memcpy(entry->agent_name, peer->agent_name, REGISTER_AGENT_NAME_SIZE);
        memcpy(entry->fingerprint_hex, peer->fingerprint_hex, IDENTITY_FINGERPRINT_HEX_SIZE);
    }
}

void AdminViews_Clients(
    const InterfaceRegistry *registry,
    const PeerDirectory *directory,
    const char *agent_name_filter,
    uint16_t offset,
    AdminClientsReplyMessage *reply
)
{
    const HubPeer *peer;
    const ChannelBinding *binding;
    const InterfaceEntry *interface_entry;
    const char *agent_name;
    const char *interface_name;
    bool filtered = agent_name_filter[0] != '\0';
    uint16_t skipped = 0;
    uint16_t peer_index;
    uint8_t binding_index;

    memset(reply, 0, sizeof(*reply));

    for(peer_index=0; peer_index<PEER_DIRECTORY_MAX; peer_index++) {
        peer = &directory->peers[peer_index];
        if (!peer->in_use || peer->role != kHUB_PEER_ROLE_CLIENT) {
            continue;
        }

        if (!clientHasOpenChannels(peer)) {
            if (filtered) {
                continue;
            }
            if (!appendClientRow(reply, &skipped, offset, peer->peer_id, 0, ADMIN_CLIENT_NO_CHANNEL, "", "", 0, 0)) {
                return;
            }
            continue;
        }

        for(binding_index=0; binding_index<CLIENT_SESSION_BINDINGS_MAX; binding_index++) {
            binding = &peer->session.bindings[binding_index];
            if (!binding->in_use) {
                continue;
            }
            interface_entry = InterfaceRegistry_FindById(registry, binding->interface_id);
            if (interface_entry != NULL) {
                agent_name = interface_entry->agent_name;
                interface_name = interface_entry->interface_name;
            } else {
                agent_name = binding->agent_name;
                interface_name = binding->interface_name;
            }
            if (filtered && strcmp(agent_name, agent_name_filter) != 0) {
                continue;
            }
            if (!appendClientRow(
                    reply,
                    &skipped,
                    offset,
                    peer->peer_id,
                    binding->interface_id,
                    binding->channel,
                    agent_name,
                    interface_name,
                    binding->frames_forwarded,
                    binding->frames_dropped
                )) {
                return;
            }
        }
    }
}

void AdminViews_Interfaces(
    const InterfaceRegistry *registry,
    const PeerDirectory *directory,
    uint16_t offset,
    AdminInterfacesReplyMessage *reply
)
{
    const InterfaceEntry *interface_entry;
    AdminInterfacesReplyEntry *entry;
    uint16_t skipped = 0;
    uint32_t i;

    memset(reply, 0, sizeof(*reply));

    for(i=0; i<INTERFACE_REGISTRY_MAX; i++) {
        interface_entry = &registry->entries[i];
        if (!interface_entry->in_use) {
            continue;
        }
        if (skipped < offset) {
            skipped++;
            continue;
        }
        if (reply->count == ADMIN_INTERFACES_REPLY_ENTRIES_MAX) {
            reply->flags |= ADMIN_REPLY_FLAG_MORE;
            return;
        }
        entry = &reply->entries[reply->count++];
        entry->interface_id = interface_entry->interface_id;
        entry->subscriber_count = countSubscribers(directory, interface_entry->interface_id);
        entry->frames_received = interface_entry->frames_received;
        entry->tx_dropped = interface_entry->tx_dropped;
        entry->mode = interfaceMode(interface_entry);
        memcpy(entry->agent_name, interface_entry->agent_name, REGISTER_AGENT_NAME_SIZE);
        memcpy(entry->interface_name, interface_entry->interface_name, REGISTER_INTERFACE_NAME_SIZE);
    }
}

void AdminViews_Transactions(
    const InterfaceRegistry *registry,
    const TransactionTable *transactions,
    uint64_t now_us,
    uint16_t offset,
    AdminTxnsReplyMessage *reply
)
{
    const RelayedTransaction *transaction;
    uint16_t skipped = 0;
    uint16_t i;

    memset(reply, 0, sizeof(*reply));

    for(i=0; i<TRANSACTION_TABLE_MAX; i++) {
        transaction = &transactions->entries[i];
        if (transaction->state == kTRANSACTION_RELAY_STATE_FREE) {
            continue;
        }
        if (skipped < offset) {
            skipped++;
            continue;
        }
        if (reply->count == ADMIN_TXNS_REPLY_ENTRIES_MAX) {
            reply->flags |= ADMIN_REPLY_FLAG_MORE;
            return;
        }
        fillTransactionEntry(registry, transaction, now_us, &reply->entries[reply->count++]);
    }
}

/* ---------- private ---------- */

static uint8_t countAgentInterfaces(const InterfaceRegistry *registry, uint32_t agent_peer_id)
{
    uint8_t count = 0;
    uint32_t i;

    for(i=0; i<INTERFACE_REGISTRY_MAX; i++) {
        if (registry->entries[i].in_use && registry->entries[i].agent_peer_id == agent_peer_id) {
            count++;
        }
    }

    return count;
}

static uint8_t countSubscribers(const PeerDirectory *directory, uint32_t interface_id)
{
    uint8_t channel;
    uint8_t count = 0;
    uint16_t i;

    for(i=0; i<PEER_DIRECTORY_MAX; i++) {
        if (!directory->peers[i].in_use || directory->peers[i].role != kHUB_PEER_ROLE_CLIENT) {
            continue;
        }
        if (ClientSession_ChannelForInterface(&directory->peers[i].session, interface_id, &channel)) {
            count++;
        }
    }

    return count;
}

static bool clientHasOpenChannels(const HubPeer *peer)
{
    uint8_t i;

    for(i=0; i<CLIENT_SESSION_BINDINGS_MAX; i++) {
        if (peer->session.bindings[i].in_use) {
            return true;
        }
    }

    return false;
}

static bool appendClientRow(
    AdminClientsReplyMessage *reply,
    uint16_t *skipped,
    uint16_t offset,
    uint32_t peer_id,
    uint32_t interface_id,
    uint8_t channel,
    const char *agent_name,
    const char *interface_name,
    uint32_t frames_forwarded,
    uint32_t frames_dropped
)
{
    AdminClientsReplyEntry *entry;

    if (*skipped < offset) {
        (*skipped)++;
        return true;
    }
    if (reply->count == ADMIN_CLIENTS_REPLY_ENTRIES_MAX) {
        reply->flags |= ADMIN_REPLY_FLAG_MORE;
        return false;
    }

    entry = &reply->entries[reply->count++];
    memset(entry, 0, sizeof(*entry));
    entry->peer_id = peer_id;
    entry->interface_id = interface_id;
    entry->channel = channel;
    BoundedString_Copy(entry->agent_name, sizeof(entry->agent_name), agent_name);
    BoundedString_Copy(entry->interface_name, sizeof(entry->interface_name), interface_name);
    entry->frames_forwarded = frames_forwarded;
    entry->frames_dropped = frames_dropped;

    return true;
}

static uint8_t interfaceMode(const InterfaceEntry *entry)
{
    if (!entry->fd_reported) {
        return ADMIN_INTERFACE_MODE_UNKNOWN;
    }

    return entry->fd_capable ? ADMIN_INTERFACE_MODE_FD : ADMIN_INTERFACE_MODE_CLASSIC;
}

static void fillTransactionEntry(
    const InterfaceRegistry *registry,
    const RelayedTransaction *transaction,
    uint64_t now_us,
    AdminTxnsReplyEntry *entry
)
{
    const InterfaceEntry *interface_entry = InterfaceRegistry_FindById(registry, transaction->interface_id);

    entry->interface_id = transaction->interface_id;
    entry->client_peer_id = transaction->client_peer_id;
    entry->agent_peer_id = transaction->agent_peer_id;
    entry->txn_id = transaction->txn_id;
    entry->state = transaction->state == kTRANSACTION_RELAY_STATE_ACTIVE ? ADMIN_TXN_STATE_ACTIVE : ADMIN_TXN_STATE_PENDING;
    entry->flags = transaction->exclusive ? ADMIN_TXN_FLAG_EXCLUSIVE : 0;
    entry->completed_steps = transaction->completed_steps;
    entry->bytes_in_flight = transaction->relayed_bytes - transaction->consumed_bytes;
    entry->age_ms = elapsedMs(transaction->started_at_us, now_us);
    entry->progress_age_ms = ADMIN_TXN_NO_PROGRESS;
    if (transaction->last_progress_us != 0) {
        entry->progress_age_ms = elapsedMs(transaction->last_progress_us, now_us);
    }
    if (interface_entry != NULL) {
        memcpy(entry->agent_name, interface_entry->agent_name, REGISTER_AGENT_NAME_SIZE);
        memcpy(entry->interface_name, interface_entry->interface_name, REGISTER_INTERFACE_NAME_SIZE);
    }
}

static uint32_t elapsedMs(uint64_t since_us, uint64_t now_us)
{
    uint64_t elapsed_ms;

    if (now_us <= since_us) {
        return 0;
    }
    elapsed_ms = (now_us - since_us) / MICROSECONDS_PER_MILLISECOND;
    if (elapsed_ms > UINT32_MAX) {
        return UINT32_MAX;
    }
    return (uint32_t)elapsed_ms;
}
