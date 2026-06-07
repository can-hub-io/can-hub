#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CLIENT_SESSION_BINDINGS_MAX 32

/*
 * One client's session with the hub: the interfaces it holds open, each
 * bound to a channel that is unique within the session.
 */
typedef struct {
    bool in_use;
    bool suppress_echo;
    uint32_t interface_id;
    uint8_t channel;
} ChannelBinding;

typedef struct {
    ChannelBinding bindings[CLIENT_SESSION_BINDINGS_MAX];
    uint8_t next_channel;
} ClientSession;

void ClientSession_Reset(ClientSession *self);
bool ClientSession_OpenInterface(ClientSession *self, uint32_t interface_id, bool suppress_echo, uint8_t *channel);
void ClientSession_CloseChannel(ClientSession *self, uint8_t channel);
bool ClientSession_InterfaceForChannel(const ClientSession *self, uint8_t channel, uint32_t *interface_id);
bool ClientSession_ChannelForInterface(const ClientSession *self, uint32_t interface_id, uint8_t *channel);
const ChannelBinding *ClientSession_BindingForInterface(const ClientSession *self, uint32_t interface_id);
void ClientSession_RemoveInterface(ClientSession *self, uint32_t interface_id);
