#include "domain/interface_registry.h"

#include <string.h>

static bool isRegistrationColliding(const InterfaceRegistry *self, const RegisterMessage *registration);
static InterfaceEntry *findFreeEntry(InterfaceRegistry *self);
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

static bool namesMatch(const InterfaceEntry *entry, const char *agent_name, const char *interface_name)
{
    return strcmp(entry->agent_name, agent_name) == 0 && strcmp(entry->interface_name, interface_name) == 0;
}
