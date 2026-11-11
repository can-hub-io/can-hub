#include "hub/domain/peer_directory.h"

#include <string.h>

static bool displacesBefore(const HubPeer *peer, const HubPeer *other);
static void forgetLiveSlot(PeerDirectory *self, uint16_t slot);

/* ---------- public ---------- */

void PeerDirectory_Reset(PeerDirectory *self)
{
    memset(self, 0, sizeof(*self));
    SlotIndex_Init(&self->slot_by_peer_id, self->peer_id_buckets, PEER_DIRECTORY_INDEX_BUCKETS);
}

HubPeer *PeerDirectory_Allocate(PeerDirectory *self, uint32_t peer_id)
{
    uint16_t i;

    for(i=0; i<PEER_DIRECTORY_MAX; i++) {
        if (!self->peers[i].in_use) {
            memset(&self->peers[i], 0, sizeof(self->peers[i]));
            self->peers[i].in_use = true;
            self->peers[i].peer_id = peer_id;
            ClientSession_Reset(&self->peers[i].session);
            EgressQueue_Reset(&self->peers[i].egress);
            SlotIndex_Put(&self->slot_by_peer_id, peer_id, i);
            self->live_slots[self->live_count++] = i;
            return &self->peers[i];
        }
    }

    return NULL;
}

HubPeer *PeerDirectory_Find(PeerDirectory *self, uint32_t peer_id)
{
    uint16_t slot;

    if (!SlotIndex_Find(&self->slot_by_peer_id, peer_id, &slot)) {
        return NULL;
    }

    return &self->peers[slot];
}

void PeerDirectory_Release(PeerDirectory *self, uint32_t peer_id)
{
    HubPeer *peer = PeerDirectory_Find(self, peer_id);

    if (peer == NULL) {
        return;
    }

    forgetLiveSlot(self, PeerDirectory_SlotOf(self, peer));
    SlotIndex_Remove(&self->slot_by_peer_id, peer_id);
    memset(peer, 0, sizeof(*peer));
}

HubPeer *PeerDirectory_At(PeerDirectory *self, uint16_t index)
{
    if (index >= PEER_DIRECTORY_MAX || !self->peers[index].in_use) {
        return NULL;
    }

    return &self->peers[index];
}

HubPeer *PeerDirectory_LiveAt(PeerDirectory *self, uint16_t position)
{
    if (position >= self->live_count) {
        return NULL;
    }

    return &self->peers[self->live_slots[position]];
}

uint16_t PeerDirectory_SlotOf(const PeerDirectory *self, const HubPeer *peer)
{
    return (uint16_t)(peer - self->peers);
}

HubPeer *PeerDirectory_FindAgentByName(PeerDirectory *self, const char *agent_name)
{
    uint16_t i;

    for(i=0; i<PEER_DIRECTORY_MAX; i++) {
        if (!self->peers[i].in_use || self->peers[i].role != kHUB_PEER_ROLE_AGENT) {
            continue;
        }
        if (strcmp(self->peers[i].agent_name, agent_name) == 0) {
            return &self->peers[i];
        }
    }

    return NULL;
}

HubPeer *PeerDirectory_FindDisplaceable(PeerDirectory *self)
{
    HubPeer *candidate = NULL;
    HubPeer *peer;
    uint16_t i;

    for(i=0; i<PEER_DIRECTORY_MAX; i++) {
        peer = &self->peers[i];
        if (!peer->in_use || HubPeer_Displacement(peer) == kHUB_PEER_DISPLACEMENT_NEVER) {
            continue;
        }
        if (candidate == NULL || displacesBefore(peer, candidate)) {
            candidate = peer;
        }
    }

    return candidate;
}

HubPeer *PeerDirectory_FindDisplaceableAgent(PeerDirectory *self)
{
    HubPeer *candidate = NULL;
    HubPeer *peer;
    uint16_t i;

    for(i=0; i<PEER_DIRECTORY_MAX; i++) {
        peer = &self->peers[i];
        if (!peer->in_use || HubPeer_Displacement(peer) != kHUB_PEER_DISPLACEMENT_UNVOUCHED_AGENT) {
            continue;
        }
        if (candidate == NULL || peer->connected_at_us > candidate->connected_at_us) {
            candidate = peer;
        }
    }

    return candidate;
}

bool PeerDirectory_IsFull(const PeerDirectory *self)
{
    return PeerDirectory_Count(self) == PEER_DIRECTORY_MAX;
}

uint16_t PeerDirectory_Count(const PeerDirectory *self)
{
    return self->live_count;
}

uint16_t PeerDirectory_CountRole(const PeerDirectory *self, uint8_t role)
{
    uint16_t count = 0;
    uint16_t i;

    for(i=0; i<PEER_DIRECTORY_MAX; i++) {
        if (self->peers[i].in_use && self->peers[i].role == role) {
            count++;
        }
    }

    return count;
}

uint16_t PeerDirectory_CountAwaitingHelloFrom(const PeerDirectory *self, const char *origin_host)
{
    const HubPeer *peer;
    uint16_t count = 0;
    uint16_t i;

    for(i=0; i<PEER_DIRECTORY_MAX; i++) {
        peer = &self->peers[i];
        if (peer->in_use && peer->role == kHUB_PEER_ROLE_UNKNOWN && strcmp(peer->origin_host, origin_host) == 0) {
            count++;
        }
    }

    return count;
}

void PeerDirectory_List(const PeerDirectory *self, uint16_t offset, AdminPeersReplyMessage *reply, uint64_t now_us)
{
    const HubPeer *peer;
    uint16_t skipped = 0;
    uint16_t i;

    memset(reply, 0, sizeof(*reply));

    for(i=0; i<PEER_DIRECTORY_MAX; i++) {
        peer = &self->peers[i];
        if (!peer->in_use) {
            continue;
        }
        if (skipped < offset) {
            skipped++;
            continue;
        }
        if (reply->count == ADMIN_PEERS_REPLY_ENTRIES_MAX) {
            reply->flags |= ADMIN_REPLY_FLAG_MORE;
            return;
        }
        HubPeer_FillAdminEntry(peer, &reply->entries[reply->count++], now_us);
    }
}

/* ---------- private ---------- */

static bool displacesBefore(const HubPeer *peer, const HubPeer *other)
{
    uint8_t peer_displacement = HubPeer_Displacement(peer);
    uint8_t other_displacement = HubPeer_Displacement(other);

    if (peer_displacement != other_displacement) {
        return peer_displacement < other_displacement;
    }

    return peer->connected_at_us > other->connected_at_us;
}

static void forgetLiveSlot(PeerDirectory *self, uint16_t slot)
{
    uint16_t position;

    for(position=0; position<self->live_count; position++) {
        if (self->live_slots[position] == slot) {
            self->live_slots[position] = self->live_slots[--self->live_count];
            return;
        }
    }
}
