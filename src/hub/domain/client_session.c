#include "hub/domain/client_session.h"

#include <stdio.h>
#include <string.h>

static ChannelBinding *findByChannel(ClientSession *self, uint8_t channel);
static ChannelBinding *findFree(ClientSession *self);
static bool allocateChannel(ClientSession *self, uint8_t *channel);

/* ---------- public ---------- */

void ClientSession_Reset(ClientSession *self)
{
    memset(self, 0, sizeof(*self));
}

bool ClientSession_OpenInterface(ClientSession *self, const ChannelOpenRequest *request, uint8_t *channel)
{
    ChannelBinding *binding = findFree(self);

    if (binding == NULL || !allocateChannel(self, channel)) {
        return false;
    }

    binding->in_use = true;
    binding->dormant = false;
    binding->suppress_echo = request->suppress_echo;
    binding->can_write = request->can_write;
    binding->reliable = request->reliable;
    binding->interface_id = request->interface_id;
    binding->channel = *channel;
    snprintf(binding->agent_name, sizeof(binding->agent_name), "%s", request->agent_name);
    snprintf(binding->interface_name, sizeof(binding->interface_name), "%s", request->interface_name);

    return true;
}

bool ClientSession_CanWrite(const ClientSession *self, uint8_t channel)
{
    uint8_t i;

    for(i=0; i<CLIENT_SESSION_BINDINGS_MAX; i++) {
        if (self->bindings[i].in_use && self->bindings[i].channel == channel) {
            return self->bindings[i].can_write;
        }
    }

    return false;
}

bool ClientSession_ChannelReliable(const ClientSession *self, uint8_t channel)
{
    uint8_t i;

    for(i=0; i<CLIENT_SESSION_BINDINGS_MAX; i++) {
        if (self->bindings[i].in_use && self->bindings[i].channel == channel) {
            return self->bindings[i].reliable;
        }
    }

    return false;
}

bool ClientSession_SetFilters(ClientSession *self, uint8_t channel, const CanFilter *filters, uint8_t count)
{
    ChannelBinding *binding = findByChannel(self, channel);

    if (binding == NULL || count > SUBSCRIBE_FILTERS_MAX) {
        return false;
    }

    binding->filter_count = count;
    if (count > 0) {
        memcpy(binding->filters, filters, (size_t)count * sizeof(*filters));
    }

    return true;
}

bool ClientSession_ChannelAccepts(const ClientSession *self, uint8_t channel, uint32_t can_id)
{
    uint8_t i;
    uint8_t f;

    for(i=0; i<CLIENT_SESSION_BINDINGS_MAX; i++) {
        if (!self->bindings[i].in_use || self->bindings[i].channel != channel) {
            continue;
        }
        if (self->bindings[i].filter_count == 0) {
            return true;
        }
        for(f=0; f<self->bindings[i].filter_count; f++) {
            if ((can_id & self->bindings[i].filters[f].can_mask)
                == (self->bindings[i].filters[f].can_id & self->bindings[i].filters[f].can_mask)) {
                return true;
            }
        }
        return false;
    }

    return true;
}

void ClientSession_CloseChannel(ClientSession *self, uint8_t channel)
{
    ChannelBinding *binding = findByChannel(self, channel);

    if (binding != NULL) {
        memset(binding, 0, sizeof(*binding));
    }
}

bool ClientSession_InterfaceForChannel(const ClientSession *self, uint8_t channel, uint32_t *interface_id)
{
    uint8_t i;

    for(i=0; i<CLIENT_SESSION_BINDINGS_MAX; i++) {
        if (self->bindings[i].in_use && !self->bindings[i].dormant && self->bindings[i].channel == channel) {
            *interface_id = self->bindings[i].interface_id;
            return true;
        }
    }

    return false;
}

bool ClientSession_ChannelForInterface(const ClientSession *self, uint32_t interface_id, uint8_t *channel)
{
    uint8_t i;

    for(i=0; i<CLIENT_SESSION_BINDINGS_MAX; i++) {
        if (self->bindings[i].in_use && !self->bindings[i].dormant && self->bindings[i].interface_id == interface_id) {
            *channel = self->bindings[i].channel;
            return true;
        }
    }

    return false;
}

const ChannelBinding *ClientSession_NextBindingForInterface(
    const ClientSession *self,
    uint32_t interface_id,
    uint8_t *iterator
)
{
    uint8_t i;

    for(i=*iterator; i<CLIENT_SESSION_BINDINGS_MAX; i++) {
        if (self->bindings[i].in_use && !self->bindings[i].dormant && self->bindings[i].interface_id == interface_id) {
            *iterator = (uint8_t)(i + 1);
            return &self->bindings[i];
        }
    }

    *iterator = CLIENT_SESSION_BINDINGS_MAX;
    return NULL;
}

ChannelBinding *ClientSession_BindingForChannel(ClientSession *self, uint8_t channel)
{
    return findByChannel(self, channel);
}

void ClientSession_RemoveInterface(ClientSession *self, uint32_t interface_id)
{
    uint8_t i;

    for(i=0; i<CLIENT_SESSION_BINDINGS_MAX; i++) {
        if (self->bindings[i].in_use && self->bindings[i].interface_id == interface_id) {
            memset(&self->bindings[i], 0, sizeof(self->bindings[i]));
        }
    }
}

void ClientSession_DetachInterface(ClientSession *self, uint32_t interface_id)
{
    uint8_t i;

    for(i=0; i<CLIENT_SESSION_BINDINGS_MAX; i++) {
        if (self->bindings[i].in_use && !self->bindings[i].dormant && self->bindings[i].interface_id == interface_id) {
            self->bindings[i].dormant = true;
        }
    }
}

bool ClientSession_ReattachInterface(
    ClientSession *self,
    const char *agent_name,
    const char *interface_name,
    uint32_t interface_id
)
{
    bool reattached = false;
    uint8_t i;

    for(i=0; i<CLIENT_SESSION_BINDINGS_MAX; i++) {
        if (!self->bindings[i].in_use || !self->bindings[i].dormant) {
            continue;
        }
        if (strncmp(self->bindings[i].agent_name, agent_name, REGISTER_AGENT_NAME_SIZE) != 0) {
            continue;
        }
        if (strncmp(self->bindings[i].interface_name, interface_name, REGISTER_INTERFACE_NAME_SIZE) != 0) {
            continue;
        }

        self->bindings[i].interface_id = interface_id;
        self->bindings[i].dormant = false;
        reattached = true;
    }

    return reattached;
}

/* ---------- private ---------- */

static ChannelBinding *findByChannel(ClientSession *self, uint8_t channel)
{
    uint8_t i;

    for(i=0; i<CLIENT_SESSION_BINDINGS_MAX; i++) {
        if (self->bindings[i].in_use && self->bindings[i].channel == channel) {
            return &self->bindings[i];
        }
    }

    return NULL;
}

static ChannelBinding *findFree(ClientSession *self)
{
    uint8_t i;

    for(i=0; i<CLIENT_SESSION_BINDINGS_MAX; i++) {
        if (!self->bindings[i].in_use) {
            return &self->bindings[i];
        }
    }

    return NULL;
}

static bool allocateChannel(ClientSession *self, uint8_t *channel)
{
    uint8_t attempts;

    for(attempts=0; attempts<CLIENT_SESSION_BINDINGS_MAX+1; attempts++) {
        if (findByChannel(self, self->next_channel) == NULL) {
            *channel = self->next_channel++;
            return true;
        }
        self->next_channel++;
    }

    return false;
}
