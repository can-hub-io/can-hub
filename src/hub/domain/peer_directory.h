#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hub/domain/hub_peer.h"
#include "hub/ports/hub_transport_port.h"
#include "protocol/admin_message.h"
#include "shared/slot_index.h"

#define PEER_DIRECTORY_MAX HUB_PEERS_MAX
#define PEER_DIRECTORY_INDEX_BUCKETS (2 * PEER_DIRECTORY_MAX)

/*
 * Collection of the live peers of the hub: allocation, lookup, counting
 * and the admin projection. Everything a single peer knows lives in
 * HubPeer. A peer is some 64 KB, so the directory also keeps what the data
 * path needs to avoid walking them: the slot of each peer id and the list
 * of the slots in use.
 */
typedef struct {
    HubPeer peers[PEER_DIRECTORY_MAX];
    SlotIndexBucket peer_id_buckets[PEER_DIRECTORY_INDEX_BUCKETS];
    SlotIndex slot_by_peer_id;
    uint16_t live_slots[PEER_DIRECTORY_MAX];
    uint16_t live_count;
} PeerDirectory;

void PeerDirectory_Reset(PeerDirectory *self);
HubPeer *PeerDirectory_Allocate(PeerDirectory *self, uint32_t peer_id);
HubPeer *PeerDirectory_Find(PeerDirectory *self, uint32_t peer_id);
void PeerDirectory_Release(PeerDirectory *self, uint32_t peer_id);
HubPeer *PeerDirectory_At(PeerDirectory *self, uint16_t index);
HubPeer *PeerDirectory_LiveAt(PeerDirectory *self, uint16_t position);
uint16_t PeerDirectory_SlotOf(const PeerDirectory *self, const HubPeer *peer);
HubPeer *PeerDirectory_FindAgentByName(PeerDirectory *self, const char *agent_name);
HubPeer *PeerDirectory_FindDisplaceable(PeerDirectory *self);
HubPeer *PeerDirectory_FindDisplaceableAgent(PeerDirectory *self);
bool PeerDirectory_IsFull(const PeerDirectory *self);
uint16_t PeerDirectory_Count(const PeerDirectory *self);
uint16_t PeerDirectory_CountRole(const PeerDirectory *self, uint8_t role);
uint16_t PeerDirectory_CountAwaitingHelloFrom(const PeerDirectory *self, const char *origin_host);
void PeerDirectory_List(const PeerDirectory *self, uint16_t offset, AdminPeersReplyMessage *reply, uint64_t now_us);
