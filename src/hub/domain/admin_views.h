#pragma once

#include <stdint.h>

#include "hub/domain/interface_registry.h"
#include "hub/domain/peer_directory.h"
#include "protocol/admin_message.h"

/*
 * Admin projections that join the peer directory with the interface
 * registry: live agents with their catalogue size, and the open client
 * channels resolved to agent and interface names. An empty agent name
 * filter selects everything.
 */
void AdminViews_Agents(
    const InterfaceRegistry *registry,
    const PeerDirectory *directory,
    const char *agent_name_filter,
    uint16_t offset,
    AdminAgentsReplyMessage *reply
);
void AdminViews_Clients(
    const InterfaceRegistry *registry,
    const PeerDirectory *directory,
    const char *agent_name_filter,
    uint16_t offset,
    AdminClientsReplyMessage *reply
);
void AdminViews_Interfaces(
    const InterfaceRegistry *registry,
    const PeerDirectory *directory,
    uint16_t offset,
    AdminInterfacesReplyMessage *reply
);
