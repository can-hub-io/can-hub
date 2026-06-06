#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hub/domain/client_session.h"
#include "hub/ports/identity_store_port.h"

#define PEER_DIRECTORY_MAX 64

/*
 * Live peers of the hub. A peer is unknown until its HELLO declares it an
 * agent or a client; client peers carry their session.
 */
typedef enum thub_peer_role_e {
    kHUB_PEER_ROLE_UNKNOWN = 0,
    kHUB_PEER_ROLE_AGENT,
    kHUB_PEER_ROLE_CLIENT,
    kHUB_PEER_ROLE_MAX,
} THUB_PEER_ROLE;

typedef struct {
    bool in_use;
    uint32_t peer_id;
    uint8_t role;
    char fingerprint_hex[IDENTITY_FINGERPRINT_HEX_SIZE];
    ClientSession session;
} HubPeer;

typedef struct {
    HubPeer peers[PEER_DIRECTORY_MAX];
} PeerDirectory;

void PeerDirectory_Reset(PeerDirectory *self);
HubPeer *PeerDirectory_Allocate(PeerDirectory *self, uint32_t peer_id);
HubPeer *PeerDirectory_Find(PeerDirectory *self, uint32_t peer_id);
void PeerDirectory_Release(PeerDirectory *self, uint32_t peer_id);
HubPeer *PeerDirectory_At(PeerDirectory *self, uint8_t index);
