#include "hub/domain/interface_registry.h"

#include <string.h>

#define PACE_EFFECTIVE_PERCENT 80
#define PACE_BURST_BITS 8192

static bool isRegistrationColliding(const InterfaceRegistry *self, const RegisterMessage *registration);
static InterfaceEntry *findFreeEntry(InterfaceRegistry *self);
static InterfaceEntry *findMutableByAgentChannel(InterfaceRegistry *self, uint32_t agent_peer_id, uint8_t agent_channel);
static bool namesMatch(const InterfaceEntry *entry, const char *agent_name, const char *interface_name);

/* ---------- public ---------- */

void InterfaceRegistry_Reset(InterfaceRegistry *self)
{
    memset(self, 0, sizeof(*self));
    self->next_interface_id = 1;
}

bool InterfaceRegistry_RegisterAgent(
    InterfaceRegistry *self,
    uint32_t agent_peer_id,
    const RegisterMessage *registration,
    RegisterAckMessage *ack
)
{
    InterfaceEntry *entry;
    uint8_t i;

    memset(ack, 0, sizeof(*ack));
    ack->interface_count = registration->interface_count;

    if (isRegistrationColliding(self, registration)) {
        ack->status = 1;
        return false;
    }

    for(i=0; i<registration->interface_count; i++) {
        entry = findFreeEntry(self);
        if (entry == NULL) {
            InterfaceRegistry_RemovePeer(self, agent_peer_id);
            ack->status = 1;
            return false;
        }

        entry->in_use = true;
        entry->interface_id = self->next_interface_id++;
        entry->agent_peer_id = agent_peer_id;
        entry->agent_channel = i;
        memcpy(entry->agent_name, registration->agent_name, REGISTER_AGENT_NAME_SIZE);
        memcpy(entry->interface_name, registration->interface_names[i], REGISTER_INTERFACE_NAME_SIZE);

        ack->channels[i] = entry->agent_channel;
    }

    ack->status = REGISTER_STATUS_OK;

    return true;
}

void InterfaceRegistry_RemovePeer(InterfaceRegistry *self, uint32_t agent_peer_id)
{
    uint32_t i;

    for(i=0; i<INTERFACE_REGISTRY_MAX; i++) {
        if (self->entries[i].in_use && self->entries[i].agent_peer_id == agent_peer_id) {
            memset(&self->entries[i], 0, sizeof(self->entries[i]));
        }
    }
}

bool InterfaceRegistry_CollidingPeer(
    const InterfaceRegistry *self,
    const RegisterMessage *registration,
    uint32_t *agent_peer_id
)
{
    uint32_t i;
    uint8_t name_index;

    for(i=0; i<INTERFACE_REGISTRY_MAX; i++) {
        if (!self->entries[i].in_use) {
            continue;
        }
        for(name_index=0; name_index<registration->interface_count; name_index++) {
            if (namesMatch(&self->entries[i], registration->agent_name, registration->interface_names[name_index])) {
                *agent_peer_id = self->entries[i].agent_peer_id;
                return true;
            }
        }
    }

    return false;
}

const InterfaceEntry *InterfaceRegistry_FindById(const InterfaceRegistry *self, uint32_t interface_id)
{
    uint32_t i;

    for(i=0; i<INTERFACE_REGISTRY_MAX; i++) {
        if (self->entries[i].in_use && self->entries[i].interface_id == interface_id) {
            return &self->entries[i];
        }
    }

    return NULL;
}

const InterfaceEntry *InterfaceRegistry_FindByName(
    const InterfaceRegistry *self,
    const char *agent_name,
    const char *interface_name
)
{
    uint32_t i;

    for(i=0; i<INTERFACE_REGISTRY_MAX; i++) {
        if (self->entries[i].in_use && namesMatch(&self->entries[i], agent_name, interface_name)) {
            return &self->entries[i];
        }
    }

    return NULL;
}

const InterfaceEntry *InterfaceRegistry_FindByAgentChannel(
    const InterfaceRegistry *self,
    uint32_t agent_peer_id,
    uint8_t agent_channel
)
{
    uint32_t i;

    for(i=0; i<INTERFACE_REGISTRY_MAX; i++) {
        if (self->entries[i].in_use
            && self->entries[i].agent_peer_id == agent_peer_id
            && self->entries[i].agent_channel == agent_channel) {
            return &self->entries[i];
        }
    }

    return NULL;
}

void InterfaceRegistry_List(const InterfaceRegistry *self, uint16_t offset, ListReplyMessage *reply)
{
    uint32_t i;
    uint16_t live_index = 0;

    memset(reply, 0, sizeof(*reply));

    for(i=0; i<INTERFACE_REGISTRY_MAX; i++) {
        if (!self->entries[i].in_use) {
            continue;
        }
        if (live_index < offset) {
            live_index++;
            continue;
        }
        if (reply->count == LIST_REPLY_ENTRIES_MAX) {
            reply->flags |= LIST_REPLY_FLAG_MORE;
            return;
        }

        reply->entries[reply->count].interface_id = self->entries[i].interface_id;
        memcpy(reply->entries[reply->count].agent_name, self->entries[i].agent_name, REGISTER_AGENT_NAME_SIZE);
        memcpy(
            reply->entries[reply->count].interface_name,
            self->entries[i].interface_name,
            REGISTER_INTERFACE_NAME_SIZE
        );
        reply->count++;
        live_index++;
    }
}

void InterfaceRegistry_CountFrame(InterfaceRegistry *self, uint32_t interface_id)
{
    uint32_t i;

    for(i=0; i<INTERFACE_REGISTRY_MAX; i++) {
        if (self->entries[i].in_use && self->entries[i].interface_id == interface_id) {
            self->entries[i].frames_received++;
            return;
        }
    }
}

void InterfaceRegistry_SetTxDropped(
    InterfaceRegistry *self,
    uint32_t agent_peer_id,
    uint8_t agent_channel,
    uint64_t tx_dropped
)
{
    InterfaceEntry *entry = findMutableByAgentChannel(self, agent_peer_id, agent_channel);

    if (entry != NULL) {
        entry->tx_dropped = tx_dropped;
    }
}

void InterfaceRegistry_ApplyAdvertisedRate(
    InterfaceRegistry *self,
    uint32_t agent_peer_id,
    uint8_t agent_channel,
    uint32_t advertised_rate,
    uint32_t credit,
    uint64_t now_us
)
{
    InterfaceEntry *entry = findMutableByAgentChannel(self, agent_peer_id, agent_channel);
    uint32_t effective_rate = credit != 0 ? credit : advertised_rate;
    uint32_t paced_rate = (uint32_t)((uint64_t)effective_rate * PACE_EFFECTIVE_PERCENT / 100);

    if (entry == NULL) {
        return;
    }

    entry->advertised_rate = advertised_rate;
    if (entry->shaper.burst_bits == 0) {
        EgressShaper_Init(&entry->shaper, now_us, paced_rate, PACE_BURST_BITS);
    } else {
        EgressShaper_SetRate(&entry->shaper, paced_rate);
    }
}

bool InterfaceRegistry_TryEgress(
    InterfaceRegistry *self,
    uint32_t agent_peer_id,
    uint8_t agent_channel,
    uint64_t bits,
    uint64_t now_us
)
{
    InterfaceEntry *entry = findMutableByAgentChannel(self, agent_peer_id, agent_channel);

    if (entry == NULL) {
        return true;
    }

    EgressShaper_Refill(&entry->shaper, now_us);

    return EgressShaper_TryConsume(&entry->shaper, bits);
}

uint64_t InterfaceRegistry_EgressDelayUs(
    InterfaceRegistry *self,
    uint32_t agent_peer_id,
    uint8_t agent_channel,
    uint64_t bits,
    uint64_t now_us
)
{
    InterfaceEntry *entry = findMutableByAgentChannel(self, agent_peer_id, agent_channel);

    if (entry == NULL) {
        return 0;
    }

    EgressShaper_Refill(&entry->shaper, now_us);

    return EgressShaper_DelayUs(&entry->shaper, bits);
}

uint16_t InterfaceRegistry_Count(const InterfaceRegistry *self)
{
    uint16_t count = 0;
    uint32_t i;

    for(i=0; i<INTERFACE_REGISTRY_MAX; i++) {
        if (self->entries[i].in_use) {
            count++;
        }
    }

    return count;
}

/* ---------- private ---------- */

static bool isRegistrationColliding(const InterfaceRegistry *self, const RegisterMessage *registration)
{
    const char *candidate_name;
    uint32_t i;
    uint8_t name_index;

    for(i=0; i<INTERFACE_REGISTRY_MAX; i++) {
        if (!self->entries[i].in_use) {
            continue;
        }
        for(name_index=0; name_index<registration->interface_count; name_index++) {
            candidate_name = registration->interface_names[name_index];
            if (namesMatch(&self->entries[i], registration->agent_name, candidate_name)) {
                return true;
            }
        }
    }

    return false;
}

static InterfaceEntry *findFreeEntry(InterfaceRegistry *self)
{
    uint32_t i;

    for(i=0; i<INTERFACE_REGISTRY_MAX; i++) {
        if (!self->entries[i].in_use) {
            return &self->entries[i];
        }
    }

    return NULL;
}

static InterfaceEntry *findMutableByAgentChannel(InterfaceRegistry *self, uint32_t agent_peer_id, uint8_t agent_channel)
{
    uint32_t i;

    for(i=0; i<INTERFACE_REGISTRY_MAX; i++) {
        if (self->entries[i].in_use
            && self->entries[i].agent_peer_id == agent_peer_id
            && self->entries[i].agent_channel == agent_channel) {
            return &self->entries[i];
        }
    }

    return NULL;
}

static bool namesMatch(const InterfaceEntry *entry, const char *agent_name, const char *interface_name)
{
    return strcmp(entry->agent_name, agent_name) == 0 && strcmp(entry->interface_name, interface_name) == 0;
}
