#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "protocol/subscribe_message.h"

#define CLIENT_SESSION_BINDINGS_MAX 32

/*
 * One client's session with the hub: the interfaces it holds open, each
 * bound to a channel that is unique within the session.
 */
typedef struct {
    bool in_use;
    bool suppress_echo;
    bool can_write;
    uint32_t interface_id;
    uint8_t channel;
    uint8_t filter_count;
    uint32_t frames_forwarded;
    uint32_t frames_dropped;
    CanFilter filters[SUBSCRIBE_FILTERS_MAX];
} ChannelBinding;

typedef struct {
    ChannelBinding bindings[CLIENT_SESSION_BINDINGS_MAX];
    uint8_t next_channel;
} ClientSession;

void ClientSession_Reset(ClientSession *self);
bool ClientSession_OpenInterface(
    ClientSession *self,
    uint32_t interface_id,
    bool suppress_echo,
    bool can_write,
    uint8_t *channel
);
bool ClientSession_CanWrite(const ClientSession *self, uint8_t channel);
bool ClientSession_SetFilters(ClientSession *self, uint8_t channel, const CanFilter *filters, uint8_t count);
bool ClientSession_ChannelAccepts(const ClientSession *self, uint8_t channel, uint32_t can_id);
void ClientSession_CloseChannel(ClientSession *self, uint8_t channel);
bool ClientSession_InterfaceForChannel(const ClientSession *self, uint8_t channel, uint32_t *interface_id);
bool ClientSession_ChannelForInterface(const ClientSession *self, uint32_t interface_id, uint8_t *channel);
const ChannelBinding *ClientSession_BindingForInterface(const ClientSession *self, uint32_t interface_id);
ChannelBinding *ClientSession_BindingForChannel(ClientSession *self, uint8_t channel);
void ClientSession_RemoveInterface(ClientSession *self, uint32_t interface_id);
