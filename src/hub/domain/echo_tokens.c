#include "hub/domain/echo_tokens.h"

#include <stdbool.h>
#include <string.h>

#define NO_OWNER 0

static bool findOwnerIndex(const EchoTokens *self, uint32_t client_peer_id, uint8_t *index);
static bool claimFreeIndex(EchoTokens *self, uint8_t *index);
static bool isTokenValid(uint8_t token);

/* ---------- public ---------- */

void EchoTokens_Reset(EchoTokens *self)
{
    memset(self, 0, sizeof(*self));
}

uint8_t EchoTokens_Issue(EchoTokens *self, uint32_t client_peer_id)
{
    uint8_t index;

    if (client_peer_id == NO_OWNER) {
        return FRAME_ROUTE_NO_TOKEN;
    }
    if (findOwnerIndex(self, client_peer_id, &index)) {
        return (uint8_t)(index + 1);
    }
    if (!claimFreeIndex(self, &index)) {
        return FRAME_ROUTE_NO_TOKEN;
    }

    self->owner_peer_ids[index] = client_peer_id;

    return (uint8_t)(index + 1);
}

uint32_t EchoTokens_Originator(const EchoTokens *self, uint8_t route_flags)
{
    uint8_t token = (uint8_t)((route_flags & FRAME_ROUTE_TOKEN_MASK) >> FRAME_ROUTE_TOKEN_SHIFT);

    if ((route_flags & FRAME_ROUTE_FLAG_ECHO) == 0) {
        return NO_OWNER;
    }
    if (!isTokenValid(token)) {
        return NO_OWNER;
    }

    return self->owner_peer_ids[token - 1];
}

void EchoTokens_ReleaseClient(EchoTokens *self, uint32_t client_peer_id)
{
    uint8_t index;

    if (findOwnerIndex(self, client_peer_id, &index)) {
        self->owner_peer_ids[index] = NO_OWNER;
    }
}

/* ---------- private ---------- */

static bool findOwnerIndex(const EchoTokens *self, uint32_t client_peer_id, uint8_t *index)
{
    uint8_t i;

    for(i=0; i<FRAME_ROUTE_TOKEN_VALUES_MAX; i++) {
        if (self->owner_peer_ids[i] == client_peer_id) {
            *index = i;
            return true;
        }
    }

    return false;
}

static bool claimFreeIndex(EchoTokens *self, uint8_t *index)
{
    uint8_t candidate;
    uint8_t i;

    for(i=0; i<FRAME_ROUTE_TOKEN_VALUES_MAX; i++) {
        candidate = (uint8_t)((self->next_index + i) % FRAME_ROUTE_TOKEN_VALUES_MAX);
        if (self->owner_peer_ids[candidate] == NO_OWNER) {
            *index = candidate;
            self->next_index = (uint8_t)((candidate + 1) % FRAME_ROUTE_TOKEN_VALUES_MAX);
            return true;
        }
    }

    return false;
}

static bool isTokenValid(uint8_t token)
{
    return token != FRAME_ROUTE_NO_TOKEN && token <= FRAME_ROUTE_TOKEN_VALUES_MAX;
}
