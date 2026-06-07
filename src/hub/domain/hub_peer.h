#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hub/domain/client_session.h"
#include "hub/ports/identity_store_port.h"
#include "protocol/admin_message.h"
#include "protocol/register_message.h"

/*
 * One live peer of the hub. Unknown until its HELLO declares a role; the
 * admin role is only adoptable by local peers (unix socket). Role values
 * match the wire roles of HELLO. Agent peers carry their registered name,
 * client peers their session.
 */
typedef enum thub_peer_role_e {
    kHUB_PEER_ROLE_UNKNOWN = 0,
    kHUB_PEER_ROLE_AGENT,
    kHUB_PEER_ROLE_CLIENT,
    kHUB_PEER_ROLE_ADMIN,
    kHUB_PEER_ROLE_MAX,
} THUB_PEER_ROLE;

typedef struct {
    bool in_use;
    bool local;
    bool send_failed;
    uint32_t peer_id;
    uint8_t role;
    uint64_t hello_deadline_us;
    uint32_t frames_forwarded;
    uint32_t frames_dropped;
    char fingerprint_hex[IDENTITY_FINGERPRINT_HEX_SIZE];
    char agent_name[REGISTER_AGENT_NAME_SIZE];
    ClientSession session;
} HubPeer;

bool HubPeer_AdoptRole(HubPeer *self, uint8_t wire_role);
void HubPeer_SetAgentName(HubPeer *self, const char *agent_name);
void HubPeer_FillAdminEntry(const HubPeer *self, AdminPeersReplyEntry *entry);
