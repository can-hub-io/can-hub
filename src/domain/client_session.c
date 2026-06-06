#include "domain/client_session.h"

#include <string.h>

static ChannelBinding *findByChannel(ClientSession *self, uint8_t channel);
static ChannelBinding *findFree(ClientSession *self);
static bool allocateChannel(ClientSession *self, uint8_t *channel);

/* ---------- public ---------- */

void ClientSession_Reset(ClientSession *self)
{
    memset(self, 0, sizeof(*self));
}

bool ClientSession_OpenInterface(ClientSession *self, uint32_t interface_id, uint8_t *channel)
{
    ChannelBinding *binding = findFree(self);

    if (binding == NULL || !allocateChannel(self, channel)) {
        return false;
    }

    binding->in_use = true;
    binding->interface_id = interface_id;
    binding->channel = *channel;

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
        if (self->bindings[i].in_use && self->bindings[i].channel == channel) {
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
        if (self->bindings[i].in_use && self->bindings[i].interface_id == interface_id) {
            *channel = self->bindings[i].channel;
            return true;
        }
    }

    return false;
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
