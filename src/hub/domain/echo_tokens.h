#pragma once

#include <stdint.h>

#include "hub/domain/peer_directory.h"
#include "protocol/frame_message.h"

/*
 * Echo attribution: a token names the directory slot of the client that
 * injected a frame, and each token remembers which agents it was handed to.
 * An echo is attributed to the client only when it comes back from one of
 * those agents, so no other agent can silence a client by guessing its token.
 */
typedef struct {
    uint32_t owner_peer_id;
    uint64_t agent_slots;
} EchoToken;

typedef struct {
    EchoToken tokens[FRAME_ROUTE_TOKEN_VALUES_MAX];
} EchoTokens;

void EchoTokens_Reset(EchoTokens *self);
uint8_t EchoTokens_Issue(EchoTokens *self, uint8_t client_slot, uint32_t client_peer_id);
void EchoTokens_RecordInjection(EchoTokens *self, uint8_t token, uint8_t agent_slot);
uint32_t EchoTokens_Originator(const EchoTokens *self, uint8_t route_flags, uint8_t agent_slot);
void EchoTokens_ReleaseAgent(EchoTokens *self, uint8_t agent_slot);
