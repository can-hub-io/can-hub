#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hub/domain/hub_peer.h"
#include "protocol/admin_message.h"

#define REJECTED_PEERS_MAX 16

/*
 * The identities the hub turned away, so an admin can authorize a device
 * that knocked instead of fetching its fingerprint out of band. One entry per
 * fingerprint and role: a peer retrying on its backoff bumps its attempt count
 * rather than filling the table. A new identity evicts the one with the fewest
 * attempts, the least recently seen among equals: a device retrying on its
 * backoff outlives a flood of one-shot certificates. In memory only; an
 * admitted identity leaves the table.
 */
typedef struct {
    bool in_use;
    uint8_t role;
    uint8_t reason;
    uint32_t attempts;
    uint64_t last_seen_us;
    char name[REGISTER_AGENT_NAME_SIZE];
    char fingerprint_hex[IDENTITY_FINGERPRINT_HEX_SIZE];
    char origin[HUB_PEER_ORIGIN_SIZE];
} RejectedPeer;

typedef struct {
    RejectedPeer entries[REJECTED_PEERS_MAX];
} RejectedPeers;

typedef struct {
    const HubPeer *peer;
    const char *name;
    uint8_t reason;
    uint64_t now_us;
} Rejection;

void RejectedPeers_Reset(RejectedPeers *self);
void RejectedPeers_Record(RejectedPeers *self, const Rejection *rejection);
void RejectedPeers_Forget(RejectedPeers *self, const HubPeer *peer);
void RejectedPeers_List(const RejectedPeers *self, uint16_t offset, AdminRejectedReplyMessage *reply, uint64_t now_us);
