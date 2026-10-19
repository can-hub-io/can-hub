#include "hub/domain/rejected_peers.h"

#include <string.h>

#include "shared/bounded_string.h"

#define MICROSECONDS_PER_SECOND 1000000u

static RejectedPeer *findEntry(RejectedPeers *self, const HubPeer *peer);
static RejectedPeer *claimEntry(RejectedPeers *self);
static bool isLessWorthKeeping(const RejectedPeer *candidate, const RejectedPeer *current);
static void fillAdminEntry(const RejectedPeer *entry, AdminRejectedReplyEntry *admin_entry, uint64_t now_us);

/* ---------- public ---------- */

void RejectedPeers_Reset(RejectedPeers *self)
{
    memset(self, 0, sizeof(*self));
}

void RejectedPeers_Record(RejectedPeers *self, const Rejection *rejection)
{
    const HubPeer *peer = rejection->peer;
    RejectedPeer *entry;

    if (peer->fingerprint_hex[0] == '\0') {
        return;
    }

    entry = findEntry(self, peer);
    if (entry == NULL) {
        entry = claimEntry(self);
        memset(entry, 0, sizeof(*entry));
        entry->in_use = true;
        entry->role = peer->role;
        memcpy(entry->fingerprint_hex, peer->fingerprint_hex, IDENTITY_FINGERPRINT_HEX_SIZE);
    }

    entry->reason = rejection->reason;
    entry->attempts++;
    entry->last_seen_us = rejection->now_us;
    BoundedString_Copy(entry->name, sizeof(entry->name), rejection->name);
    memcpy(entry->origin, peer->origin, HUB_PEER_ORIGIN_SIZE);
}

void RejectedPeers_Forget(RejectedPeers *self, const HubPeer *peer)
{
    RejectedPeer *entry = findEntry(self, peer);

    if (entry != NULL) {
        memset(entry, 0, sizeof(*entry));
    }
}

void RejectedPeers_List(const RejectedPeers *self, uint16_t offset, AdminRejectedReplyMessage *reply, uint64_t now_us)
{
    uint16_t skipped = 0;
    uint8_t i;

    memset(reply, 0, sizeof(*reply));

    for(i=0; i<REJECTED_PEERS_MAX; i++) {
        if (!self->entries[i].in_use) {
            continue;
        }
        if (skipped < offset) {
            skipped++;
            continue;
        }
        if (reply->count == ADMIN_REJECTED_REPLY_ENTRIES_MAX) {
            reply->flags |= ADMIN_REPLY_FLAG_MORE;
            return;
        }
        fillAdminEntry(&self->entries[i], &reply->entries[reply->count++], now_us);
    }
}

/* ---------- private ---------- */

static RejectedPeer *findEntry(RejectedPeers *self, const HubPeer *peer)
{
    uint8_t i;

    for(i=0; i<REJECTED_PEERS_MAX; i++) {
        if (!self->entries[i].in_use || self->entries[i].role != peer->role) {
            continue;
        }
        if (strcmp(self->entries[i].fingerprint_hex, peer->fingerprint_hex) == 0) {
            return &self->entries[i];
        }
    }

    return NULL;
}

static RejectedPeer *claimEntry(RejectedPeers *self)
{
    RejectedPeer *victim = &self->entries[0];
    uint8_t i;

    for(i=0; i<REJECTED_PEERS_MAX; i++) {
        if (!self->entries[i].in_use) {
            return &self->entries[i];
        }
        if (isLessWorthKeeping(&self->entries[i], victim)) {
            victim = &self->entries[i];
        }
    }

    return victim;
}

static bool isLessWorthKeeping(const RejectedPeer *candidate, const RejectedPeer *current)
{
    if (candidate->attempts != current->attempts) {
        return candidate->attempts < current->attempts;
    }

    return candidate->last_seen_us < current->last_seen_us;
}

static void fillAdminEntry(const RejectedPeer *entry, AdminRejectedReplyEntry *admin_entry, uint64_t now_us)
{
    memset(admin_entry, 0, sizeof(*admin_entry));
    admin_entry->role = entry->role;
    admin_entry->reason = entry->reason;
    admin_entry->attempts = entry->attempts;
    if (now_us > entry->last_seen_us) {
        admin_entry->last_seen_seconds = (uint32_t)((now_us - entry->last_seen_us) / MICROSECONDS_PER_SECOND);
    }
    memcpy(admin_entry->name, entry->name, REGISTER_AGENT_NAME_SIZE);
    memcpy(admin_entry->fingerprint_hex, entry->fingerprint_hex, IDENTITY_FINGERPRINT_HEX_SIZE);
    memcpy(admin_entry->origin, entry->origin, HUB_PEER_ORIGIN_SIZE < ADMIN_ORIGIN_SIZE ? HUB_PEER_ORIGIN_SIZE : ADMIN_ORIGIN_SIZE);
}
