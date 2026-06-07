#include "hub/domain/hub_peer.h"

#include <string.h>

#include "protocol/hello_message.h"

/* ---------- public ---------- */

bool HubPeer_AdoptRole(HubPeer *self, uint8_t wire_role)
{
    if (wire_role == kPEER_ROLE_ADMIN) {
        if (!self->local) {
            return false;
        }
        self->role = kHUB_PEER_ROLE_ADMIN;
        return true;
    }

    self->role = wire_role == kPEER_ROLE_AGENT ? kHUB_PEER_ROLE_AGENT : kHUB_PEER_ROLE_CLIENT;

    return true;
}

void HubPeer_SetAgentName(HubPeer *self, const char *agent_name)
{
    strncpy(self->agent_name, agent_name, REGISTER_AGENT_NAME_SIZE - 1);
}

void HubPeer_FillAdminEntry(const HubPeer *self, AdminPeersReplyEntry *entry)
{
    memset(entry, 0, sizeof(*entry));
    entry->peer_id = self->peer_id;
    entry->role = self->role;
    memcpy(entry->agent_name, self->agent_name, REGISTER_AGENT_NAME_SIZE);
    memcpy(entry->fingerprint_hex, self->fingerprint_hex, IDENTITY_FINGERPRINT_HEX_SIZE);
}
