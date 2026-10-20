#include "hub/domain/hub_peer.h"

#include <string.h>

#include "protocol/hello_message.h"
#include "shared/bounded_string.h"

static bool isSamePlaintextAgent(const HubPeer *self, const HubPeer *other);
static bool isUnidentified(const HubPeer *self);

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
    BoundedString_Copy(self->agent_name, sizeof(self->agent_name), agent_name);
}

bool HubPeer_IsSameAgent(const HubPeer *self, const HubPeer *other)
{
    if (self->fingerprint_hex[0] != '\0' || other->fingerprint_hex[0] != '\0') {
        return strcmp(self->fingerprint_hex, other->fingerprint_hex) == 0;
    }

    return isSamePlaintextAgent(self, other);
}

uint8_t HubPeer_Displacement(const HubPeer *self)
{
    if (self->local || self->fingerprint_hex[0] == '\0' || self->vouched) {
        return kHUB_PEER_DISPLACEMENT_NEVER;
    }
    if (isUnidentified(self)) {
        return kHUB_PEER_DISPLACEMENT_UNIDENTIFIED;
    }
    if (self->role == kHUB_PEER_ROLE_AGENT) {
        return kHUB_PEER_DISPLACEMENT_UNVOUCHED_AGENT;
    }
    if (self->role != kHUB_PEER_ROLE_CLIENT) {
        return kHUB_PEER_DISPLACEMENT_NEVER;
    }

    return ClientSession_HoldsChannels(&self->session) ? kHUB_PEER_DISPLACEMENT_ACTIVE_CLIENT : kHUB_PEER_DISPLACEMENT_IDLE_CLIENT;
}

void HubPeer_FillAdminEntry(const HubPeer *self, AdminPeersReplyEntry *entry, uint64_t now_us)
{
    memset(entry, 0, sizeof(*entry));
    entry->peer_id = self->peer_id;
    entry->role = self->role;
    entry->transport_kind = self->transport_kind;
    entry->frames_forwarded = self->frames_forwarded;
    entry->frames_dropped = self->frames_dropped;
    if (now_us > self->connected_at_us) {
        entry->uptime_seconds = (uint32_t)((now_us - self->connected_at_us) / 1000000u);
    }
    memcpy(entry->agent_name, self->agent_name, REGISTER_AGENT_NAME_SIZE);
    memcpy(entry->fingerprint_hex, self->fingerprint_hex, IDENTITY_FINGERPRINT_HEX_SIZE);
    memcpy(entry->origin, self->origin, HUB_PEER_ORIGIN_SIZE < ADMIN_ORIGIN_SIZE ? HUB_PEER_ORIGIN_SIZE : ADMIN_ORIGIN_SIZE);
}

/* ---------- private ---------- */

static bool isSamePlaintextAgent(const HubPeer *self, const HubPeer *other)
{
    if (self->transport_kind != other->transport_kind) {
        return false;
    }
    if (self->local && other->local) {
        return true;
    }

    return self->origin_host[0] != '\0' && strcmp(self->origin_host, other->origin_host) == 0;
}

static bool isUnidentified(const HubPeer *self)
{
    if (self->role == kHUB_PEER_ROLE_UNKNOWN) {
        return true;
    }

    return self->role == kHUB_PEER_ROLE_AGENT && self->agent_name[0] == '\0';
}
