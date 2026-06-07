#include "hub/domain/peer_directory.h"

#include <string.h>

/* ---------- public ---------- */

void PeerDirectory_Reset(PeerDirectory *self)
{
    memset(self, 0, sizeof(*self));
}

HubPeer *PeerDirectory_Allocate(PeerDirectory *self, uint32_t peer_id)
{
    uint8_t i;

    for(i=0; i<PEER_DIRECTORY_MAX; i++) {
        if (!self->peers[i].in_use) {
            memset(&self->peers[i], 0, sizeof(self->peers[i]));
            self->peers[i].in_use = true;
            self->peers[i].peer_id = peer_id;
            ClientSession_Reset(&self->peers[i].session);
            return &self->peers[i];
        }
    }

    return NULL;
}

HubPeer *PeerDirectory_Find(PeerDirectory *self, uint32_t peer_id)
{
    uint8_t i;

    for(i=0; i<PEER_DIRECTORY_MAX; i++) {
        if (self->peers[i].in_use && self->peers[i].peer_id == peer_id) {
            return &self->peers[i];
        }
    }

    return NULL;
}

void PeerDirectory_Release(PeerDirectory *self, uint32_t peer_id)
{
    HubPeer *peer = PeerDirectory_Find(self, peer_id);

    if (peer != NULL) {
        memset(peer, 0, sizeof(*peer));
    }
}

HubPeer *PeerDirectory_At(PeerDirectory *self, uint8_t index)
{
    if (index >= PEER_DIRECTORY_MAX || !self->peers[index].in_use) {
        return NULL;
    }

    return &self->peers[index];
}

HubPeer *PeerDirectory_FindAgentByName(PeerDirectory *self, const char *agent_name)
{
    uint8_t i;

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

uint16_t PeerDirectory_Count(const PeerDirectory *self)
{
    uint16_t count = 0;
    uint8_t i;

    for(i=0; i<PEER_DIRECTORY_MAX; i++) {
        if (self->peers[i].in_use) {
            count++;
        }
    }

    return count;
}

uint16_t PeerDirectory_CountRole(const PeerDirectory *self, uint8_t role)
{
    uint16_t count = 0;
    uint8_t i;

    for(i=0; i<PEER_DIRECTORY_MAX; i++) {
        if (self->peers[i].in_use && self->peers[i].role == role) {
            count++;
        }
    }

    return count;
}

void PeerDirectory_List(const PeerDirectory *self, uint16_t offset, AdminPeersReplyMessage *reply)
{
    const HubPeer *peer;
    uint16_t skipped = 0;
    uint8_t i;

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
        HubPeer_FillAdminEntry(peer, &reply->entries[reply->count++]);
    }
}
