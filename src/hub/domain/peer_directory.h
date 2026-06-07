#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hub/domain/hub_peer.h"
#include "protocol/admin_message.h"

#define PEER_DIRECTORY_MAX 64

/*
 * Collection of the live peers of the hub: allocation, lookup, counting
 * and the admin projection. Everything a single peer knows lives in
 * HubPeer.
 */
typedef struct {
    HubPeer peers[PEER_DIRECTORY_MAX];
} PeerDirectory;

void PeerDirectory_Reset(PeerDirectory *self);
HubPeer *PeerDirectory_Allocate(PeerDirectory *self, uint32_t peer_id);
HubPeer *PeerDirectory_Find(PeerDirectory *self, uint32_t peer_id);
void PeerDirectory_Release(PeerDirectory *self, uint32_t peer_id);
HubPeer *PeerDirectory_At(PeerDirectory *self, uint8_t index);
HubPeer *PeerDirectory_FindAgentByName(PeerDirectory *self, const char *agent_name);
uint16_t PeerDirectory_Count(const PeerDirectory *self);
uint16_t PeerDirectory_CountRole(const PeerDirectory *self, uint8_t role);
void PeerDirectory_List(const PeerDirectory *self, uint16_t offset, AdminPeersReplyMessage *reply);
