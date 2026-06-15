#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "protocol/list_message.h"
#include "protocol/register_message.h"

#define INTERFACE_REGISTRY_MAX 256

/*
 * Catalogue of every interface exported by the live agents. Global
 * interface ids are hub-assigned and never reused within a run. The
 * (agent name, interface name) pair is unique across live registrations;
 * a colliding registration is rejected whole.
 */
typedef struct {
    bool in_use;
    uint32_t interface_id;
    uint32_t agent_peer_id;
    uint8_t agent_channel;
    uint64_t frames_received;
    uint64_t tx_dropped;
    char agent_name[REGISTER_AGENT_NAME_SIZE];
    char interface_name[REGISTER_INTERFACE_NAME_SIZE];
} InterfaceEntry;

typedef struct {
    InterfaceEntry entries[INTERFACE_REGISTRY_MAX];
    uint32_t next_interface_id;
} InterfaceRegistry;

void InterfaceRegistry_Reset(InterfaceRegistry *self);
bool InterfaceRegistry_RegisterAgent(
    InterfaceRegistry *self,
    uint32_t agent_peer_id,
    const RegisterMessage *registration,
    RegisterAckMessage *ack
);
void InterfaceRegistry_RemovePeer(InterfaceRegistry *self, uint32_t agent_peer_id);
bool InterfaceRegistry_CollidingPeer(
    const InterfaceRegistry *self,
    const RegisterMessage *registration,
    uint32_t *agent_peer_id
);
const InterfaceEntry *InterfaceRegistry_FindById(const InterfaceRegistry *self, uint32_t interface_id);
const InterfaceEntry *InterfaceRegistry_FindByAgentChannel(
    const InterfaceRegistry *self,
    uint32_t agent_peer_id,
    uint8_t agent_channel
);
const InterfaceEntry *InterfaceRegistry_FindByName(
    const InterfaceRegistry *self,
    const char *agent_name,
    const char *interface_name
);
void InterfaceRegistry_List(const InterfaceRegistry *self, uint16_t offset, ListReplyMessage *reply);
uint16_t InterfaceRegistry_Count(const InterfaceRegistry *self);
void InterfaceRegistry_CountFrame(InterfaceRegistry *self, uint32_t interface_id);
void InterfaceRegistry_SetTxDropped(
    InterfaceRegistry *self,
    uint32_t agent_peer_id,
    uint8_t agent_channel,
    uint64_t tx_dropped
);
