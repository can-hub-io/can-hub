#include "hub/domain/client_session.h"

#include <string.h>

#include "protocol/frame_message.h"
#include "shared/bounded_string.h"

#define NO_BINDING 0

static ChannelBinding *findByChannel(ClientSession *self, uint8_t channel);
static const ChannelBinding *bindingOfChannel(const ClientSession *self, uint8_t channel);
static void releaseBinding(ClientSession *self, ChannelBinding *binding);
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
    binding->error_frames = request->error_frames;
    binding->interface_id = request->interface_id;
    binding->channel = *channel;
    self->binding_of_channel[*channel] = (uint8_t)(binding - self->bindings + 1);
    BoundedString_Copy(binding->agent_name, sizeof(binding->agent_name), request->agent_name);
    BoundedString_Copy(binding->interface_name, sizeof(binding->interface_name), request->interface_name);

    return true;
}

bool ClientSession_CanWrite(const ClientSession *self, uint8_t channel)
{
    const ChannelBinding *binding = bindingOfChannel(self, channel);

    return binding != NULL && binding->can_write;
}

bool ClientSession_ChannelReliable(const ClientSession *self, uint8_t channel)
{
    const ChannelBinding *binding = bindingOfChannel(self, channel);

    return binding != NULL && binding->reliable;
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
    const ChannelBinding *binding = bindingOfChannel(self, channel);
    uint8_t f;

    if (binding == NULL) {
        return true;
    }
    if (can_id & FRAME_CAN_ID_FLAG_ERR) {
        return binding->error_frames;
    }
    if (binding->filter_count == 0) {
        return true;
    }
    for(f=0; f<binding->filter_count; f++) {
        if ((can_id & binding->filters[f].can_mask) == (binding->filters[f].can_id & binding->filters[f].can_mask)) {
            return true;
        }
    }

    return false;
}

void ClientSession_CloseChannel(ClientSession *self, uint8_t channel)
{
    ChannelBinding *binding = findByChannel(self, channel);

    if (binding != NULL) {
        releaseBinding(self, binding);
    }
}

bool ClientSession_InterfaceForChannel(const ClientSession *self, uint8_t channel, uint32_t *interface_id)
{
    const ChannelBinding *binding = bindingOfChannel(self, channel);

    if (binding == NULL || binding->dormant) {
        return false;
    }

    *interface_id = binding->interface_id;

    return true;
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

const ChannelBinding *ClientSession_BindingAt(const ClientSession *self, uint8_t index)
{
    if (index >= CLIENT_SESSION_BINDINGS_MAX || !self->bindings[index].in_use || self->bindings[index].dormant) {
        return NULL;
    }

    return &self->bindings[index];
}

bool ClientSession_HoldsReliable(const ClientSession *self, uint32_t interface_id)
{
    uint8_t i;

    for(i=0; i<CLIENT_SESSION_BINDINGS_MAX; i++) {
        if (self->bindings[i].in_use && self->bindings[i].reliable && self->bindings[i].interface_id == interface_id) {
            return true;
        }
    }

    return false;
}

bool ClientSession_HoldsChannels(const ClientSession *self)
{
    uint8_t i;

    for(i=0; i<CLIENT_SESSION_BINDINGS_MAX; i++) {
        if (self->bindings[i].in_use) {
            return true;
        }
    }

    return false;
}

bool ClientSession_TakeReliableBinding(ClientSession *self, uint32_t *interface_id)
{
    uint8_t i;

    for(i=0; i<CLIENT_SESSION_BINDINGS_MAX; i++) {
        if (self->bindings[i].in_use && self->bindings[i].reliable) {
            *interface_id = self->bindings[i].interface_id;
            releaseBinding(self, &self->bindings[i]);
            return true;
        }
    }

    return false;
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
    uint32_t interface_id,
    bool can_write
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
        self->bindings[i].can_write = can_write;
        self->bindings[i].dormant = false;
        reattached = true;
    }

    return reattached;
}

/* ---------- private ---------- */

static ChannelBinding *findByChannel(ClientSession *self, uint8_t channel)
{
    uint8_t occupant = self->binding_of_channel[channel];

    if (occupant == NO_BINDING) {
        return NULL;
    }

    return &self->bindings[occupant - 1];
}

static const ChannelBinding *bindingOfChannel(const ClientSession *self, uint8_t channel)
{
    uint8_t occupant = self->binding_of_channel[channel];

    if (occupant == NO_BINDING) {
        return NULL;
    }

    return &self->bindings[occupant - 1];
}

static void releaseBinding(ClientSession *self, ChannelBinding *binding)
{
    self->binding_of_channel[binding->channel] = NO_BINDING;
    memset(binding, 0, sizeof(*binding));
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
