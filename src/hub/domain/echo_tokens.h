#pragma once

#include <stdint.h>

#include "protocol/frame_message.h"

/*
 * Echo attribution, one table per agent: a token names a client that
 * injected through that agent. Echoes are resolved against the table of the
 * agent they come back from, so no agent can silence a client of another
 * agent by guessing its token. A table holds as many writers as the route
 * flags can address; a writer past that gets no token and sees its own echo.
 * Freed tokens are handed out last, so a late echo of a departed writer
 * seldom lands on the one that replaced it. The hub itself injects along
 * bridge routes as ECHO_TOKENS_BRIDGE_OWNER, so the echo of a bridged frame
 * is recognised and never bridged again.
 */

#define ECHO_TOKENS_BRIDGE_OWNER UINT32_MAX
typedef struct {
    uint32_t owner_peer_ids[FRAME_ROUTE_TOKEN_VALUES_MAX];
    uint8_t next_index;
} EchoTokens;

void EchoTokens_Reset(EchoTokens *self);
uint8_t EchoTokens_Issue(EchoTokens *self, uint32_t client_peer_id);
uint32_t EchoTokens_Originator(const EchoTokens *self, uint8_t route_flags);
void EchoTokens_ReleaseClient(EchoTokens *self, uint32_t client_peer_id);
