#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "protocol/register_message.h"
#include "protocol/subscribe_message.h"

#define CLIENT_SESSION_BINDINGS_MAX 32

/*
 * One client's session with the hub: the interfaces it holds open, each
 * bound to a channel that is unique within the session. A binding survives
 * its agent disconnecting (it goes dormant, keeping its channel and filters)
 * and is re-pointed at the new interface id when the agent re-registers, so a
 * flapping agent does not silently strand the client.
 */
typedef struct {
    bool in_use;
    bool dormant;
    bool suppress_echo;
    bool can_write;
    bool reliable;
    uint32_t interface_id;
    uint8_t channel;
    uint8_t filter_count;
    uint32_t frames_forwarded;
    uint32_t frames_dropped;
    char agent_name[REGISTER_AGENT_NAME_SIZE];
    char interface_name[REGISTER_INTERFACE_NAME_SIZE];
    CanFilter filters[SUBSCRIBE_FILTERS_MAX];
} ChannelBinding;

typedef struct {
    ChannelBinding bindings[CLIENT_SESSION_BINDINGS_MAX];
    uint8_t next_channel;
} ClientSession;

typedef struct {
    uint32_t interface_id;
    bool suppress_echo;
    bool can_write;
    bool reliable;
    const char *agent_name;
    const char *interface_name;
} ChannelOpenRequest;

void ClientSession_Reset(ClientSession *self);
bool ClientSession_OpenInterface(ClientSession *self, const ChannelOpenRequest *request, uint8_t *channel);
bool ClientSession_CanWrite(const ClientSession *self, uint8_t channel);
bool ClientSession_ChannelReliable(const ClientSession *self, uint8_t channel);
bool ClientSession_SetFilters(ClientSession *self, uint8_t channel, const CanFilter *filters, uint8_t count);
bool ClientSession_ChannelAccepts(const ClientSession *self, uint8_t channel, uint32_t can_id);
void ClientSession_CloseChannel(ClientSession *self, uint8_t channel);
bool ClientSession_InterfaceForChannel(const ClientSession *self, uint8_t channel, uint32_t *interface_id);
bool ClientSession_ChannelForInterface(const ClientSession *self, uint32_t interface_id, uint8_t *channel);
const ChannelBinding *ClientSession_NextBindingForInterface(
    const ClientSession *self,
    uint32_t interface_id,
    uint8_t *iterator
);
ChannelBinding *ClientSession_BindingForChannel(ClientSession *self, uint8_t channel);
void ClientSession_RemoveInterface(ClientSession *self, uint32_t interface_id);
void ClientSession_DetachInterface(ClientSession *self, uint32_t interface_id);
bool ClientSession_ReattachInterface(
    ClientSession *self,
    const char *agent_name,
    const char *interface_name,
    uint32_t interface_id,
    bool can_write
);
