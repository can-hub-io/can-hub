#include "hub/domain/echo_tokens.h"

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <string.h>

static_assert(PEER_DIRECTORY_MAX <= sizeof(uint64_t) * CHAR_BIT, "agent slots exceed the injection mask");

static bool isTokenValid(uint8_t token);
static bool isAgentSlotValid(uint8_t agent_slot);
static uint64_t agentBit(uint8_t agent_slot);

/* ---------- public ---------- */

void EchoTokens_Reset(EchoTokens *self)
{
    memset(self, 0, sizeof(*self));
}

uint8_t EchoTokens_Issue(EchoTokens *self, uint8_t client_slot, uint32_t client_peer_id)
{
    EchoToken *entry;

    if (client_slot >= FRAME_ROUTE_TOKEN_VALUES_MAX) {
        return FRAME_ROUTE_NO_TOKEN;
    }

    entry = &self->tokens[client_slot];
    if (entry->owner_peer_id != client_peer_id) {
        entry->owner_peer_id = client_peer_id;
        entry->agent_slots = 0;
    }

    return (uint8_t)(client_slot + 1);
}

void EchoTokens_RecordInjection(EchoTokens *self, uint8_t token, uint8_t agent_slot)
{
    if (!isTokenValid(token) || !isAgentSlotValid(agent_slot)) {
        return;
    }

    self->tokens[token - 1].agent_slots |= agentBit(agent_slot);
}

uint32_t EchoTokens_Originator(const EchoTokens *self, uint8_t route_flags, uint8_t agent_slot)
{
    uint8_t token = (uint8_t)((route_flags & FRAME_ROUTE_TOKEN_MASK) >> FRAME_ROUTE_TOKEN_SHIFT);
    const EchoToken *entry;

    if ((route_flags & FRAME_ROUTE_FLAG_ECHO) == 0) {
        return 0;
    }
    if (!isTokenValid(token) || !isAgentSlotValid(agent_slot)) {
        return 0;
    }

    entry = &self->tokens[token - 1];
    if ((entry->agent_slots & agentBit(agent_slot)) == 0) {
        return 0;
    }

    return entry->owner_peer_id;
}

void EchoTokens_ReleaseAgent(EchoTokens *self, uint8_t agent_slot)
{
    uint8_t i;

    if (!isAgentSlotValid(agent_slot)) {
        return;
    }

    for(i=0; i<FRAME_ROUTE_TOKEN_VALUES_MAX; i++) {
        self->tokens[i].agent_slots &= ~agentBit(agent_slot);
    }
}

/* ---------- private ---------- */

static bool isTokenValid(uint8_t token)
{
    return token != FRAME_ROUTE_NO_TOKEN && token <= FRAME_ROUTE_TOKEN_VALUES_MAX;
}

static bool isAgentSlotValid(uint8_t agent_slot)
{
    return agent_slot < PEER_DIRECTORY_MAX;
}

static uint64_t agentBit(uint8_t agent_slot)
{
    return (uint64_t)1 << agent_slot;
}
