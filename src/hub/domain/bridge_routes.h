#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "protocol/register_message.h"
#include "protocol/subscribe_message.h"

#define BRIDGE_ROUTES_MAX 64
#define BRIDGE_FILTERS_MAX SUBSCRIBE_FILTERS_MAX

/*
 * Bus-to-bus routes: every frame captured on the source interface that passes
 * the filters is injected into the destination interface. Interfaces are
 * named, not numbered, so a route outlives the agents that serve it and
 * applies again when they reconnect. A route is keyed by its two endpoints;
 * setting it again replaces its filters. Filters follow the client ones:
 * none passes everything, otherwise a frame passes when its id matches one
 * id/mask pair. Error frames never pass.
 */
typedef struct {
    char agent_name[REGISTER_AGENT_NAME_SIZE];
    char interface_name[REGISTER_INTERFACE_NAME_SIZE];
} BridgeEndpoint;

typedef struct {
    BridgeEndpoint source;
    BridgeEndpoint destination;
    uint8_t filter_count;
    CanFilter filters[BRIDGE_FILTERS_MAX];
} BridgeRoute;

typedef struct {
    bool in_use;
    BridgeRoute route;
    uint64_t forwarded;
    uint64_t dropped;
} BridgeRouteSlot;

typedef struct {
    BridgeRouteSlot slots[BRIDGE_ROUTES_MAX];
    uint8_t count;
} BridgeRoutes;

bool BridgeRoute_IsValid(const BridgeRoute *route);
bool BridgeRoute_Passes(const BridgeRoute *route, uint32_t can_id);
bool BridgeEndpoint_Equals(const BridgeEndpoint *self, const BridgeEndpoint *other);
bool BridgeEndpoint_Names(const BridgeEndpoint *self, const char *agent_name, const char *interface_name);

void BridgeRoutes_Reset(BridgeRoutes *self);
bool BridgeRoutes_Set(BridgeRoutes *self, const BridgeRoute *route);
bool BridgeRoutes_Remove(BridgeRoutes *self, const BridgeEndpoint *source, const BridgeEndpoint *destination);
const BridgeRouteSlot *BridgeRoutes_Find(
    const BridgeRoutes *self,
    const BridgeEndpoint *source,
    const BridgeEndpoint *destination
);
bool BridgeRoutes_IsEmpty(const BridgeRoutes *self);
bool BridgeRoutes_HasRoom(const BridgeRoutes *self, const BridgeRoute *route);
uint8_t BridgeRoutes_Matching(
    const BridgeRoutes *self,
    const char *agent_name,
    const char *interface_name,
    uint32_t can_id,
    uint8_t *slot_indices
);
BridgeRouteSlot *BridgeRoutes_Slot(BridgeRoutes *self, uint8_t slot_index);
const BridgeRouteSlot *BridgeRoutes_Nth(const BridgeRoutes *self, uint16_t position);
