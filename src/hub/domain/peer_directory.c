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
