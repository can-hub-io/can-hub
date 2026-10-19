#include "hub/domain/bridge_routes.h"

#include <string.h>

#include "protocol/frame_message.h"

static bool findIndex(const BridgeRoutes *self, const BridgeEndpoint *source, const BridgeEndpoint *destination, uint8_t *index);
static bool freeIndex(const BridgeRoutes *self, uint8_t *index);
static bool endpointIsNamed(const BridgeEndpoint *endpoint);

/* ---------- public ---------- */

bool BridgeRoute_IsValid(const BridgeRoute *route)
{
    if (!endpointIsNamed(&route->source) || !endpointIsNamed(&route->destination)) {
        return false;
    }
    if (BridgeEndpoint_Equals(&route->source, &route->destination)) {
        return false;
    }

    return route->filter_count <= BRIDGE_FILTERS_MAX;
}

bool BridgeRoute_Passes(const BridgeRoute *route, uint32_t can_id)
{
    uint8_t i;

    if (can_id & FRAME_CAN_ID_FLAG_ERR) {
        return false;
    }
    if (route->filter_count == 0) {
        return true;
    }

    for(i=0; i<route->filter_count; i++) {
        if ((can_id & route->filters[i].can_mask) == (route->filters[i].can_id & route->filters[i].can_mask)) {
            return true;
        }
    }

    return false;
}

bool BridgeEndpoint_Equals(const BridgeEndpoint *self, const BridgeEndpoint *other)
{
    return BridgeEndpoint_Names(self, other->agent_name, other->interface_name);
}

bool BridgeEndpoint_Names(const BridgeEndpoint *self, const char *agent_name, const char *interface_name)
{
    return strcmp(self->agent_name, agent_name) == 0 && strcmp(self->interface_name, interface_name) == 0;
}

void BridgeRoutes_Reset(BridgeRoutes *self)
{
    memset(self, 0, sizeof(*self));
}

bool BridgeRoutes_Set(BridgeRoutes *self, const BridgeRoute *route)
{
    BridgeRouteSlot *slot;
    uint8_t index;

    if (!BridgeRoute_IsValid(route)) {
        return false;
    }
    if (!findIndex(self, &route->source, &route->destination, &index) && !freeIndex(self, &index)) {
        return false;
    }

    slot = &self->slots[index];
    if (!slot->in_use) {
        memset(slot, 0, sizeof(*slot));
        slot->in_use = true;
        self->count++;
    }
    slot->route = *route;

    return true;
}

bool BridgeRoutes_Remove(BridgeRoutes *self, const BridgeEndpoint *source, const BridgeEndpoint *destination)
{
    uint8_t index;

    if (!findIndex(self, source, destination, &index)) {
        return false;
    }

    memset(&self->slots[index], 0, sizeof(self->slots[index]));
    self->count--;

    return true;
}

const BridgeRouteSlot *BridgeRoutes_Find(
    const BridgeRoutes *self,
    const BridgeEndpoint *source,
    const BridgeEndpoint *destination
)
{
    uint8_t index;

    if (!findIndex(self, source, destination, &index)) {
        return NULL;
    }

    return &self->slots[index];
}

bool BridgeRoutes_IsEmpty(const BridgeRoutes *self)
{
    return self->count == 0;
}

bool BridgeRoutes_HasRoom(const BridgeRoutes *self, const BridgeRoute *route)
{
    uint8_t index;

    return findIndex(self, &route->source, &route->destination, &index) || freeIndex(self, &index);
}

uint8_t BridgeRoutes_Matching(
    const BridgeRoutes *self,
    const char *agent_name,
    const char *interface_name,
    uint32_t can_id,
    uint8_t *slot_indices
)
{
    const BridgeRouteSlot *slot;
    uint8_t count = 0;
    uint8_t i;

    for(i=0; i<BRIDGE_ROUTES_MAX; i++) {
        slot = &self->slots[i];
        if (!slot->in_use || !BridgeEndpoint_Names(&slot->route.source, agent_name, interface_name)) {
            continue;
        }
        if (BridgeRoute_Passes(&slot->route, can_id)) {
            slot_indices[count++] = i;
        }
    }

    return count;
}

BridgeRouteSlot *BridgeRoutes_Slot(BridgeRoutes *self, uint8_t slot_index)
{
    if (slot_index >= BRIDGE_ROUTES_MAX || !self->slots[slot_index].in_use) {
        return NULL;
    }

    return &self->slots[slot_index];
}

const BridgeRouteSlot *BridgeRoutes_Nth(const BridgeRoutes *self, uint16_t position)
{
    uint16_t seen = 0;
    uint8_t i;

    for(i=0; i<BRIDGE_ROUTES_MAX; i++) {
        if (!self->slots[i].in_use) {
            continue;
        }
        if (seen == position) {
            return &self->slots[i];
        }
        seen++;
    }

    return NULL;
}

/* ---------- private ---------- */

static bool findIndex(const BridgeRoutes *self, const BridgeEndpoint *source, const BridgeEndpoint *destination, uint8_t *index)
{
    uint8_t i;

    for(i=0; i<BRIDGE_ROUTES_MAX; i++) {
        if (self->slots[i].in_use
            && BridgeEndpoint_Equals(&self->slots[i].route.source, source)
            && BridgeEndpoint_Equals(&self->slots[i].route.destination, destination)) {
            *index = i;
            return true;
        }
    }

    return false;
}

static bool freeIndex(const BridgeRoutes *self, uint8_t *index)
{
    uint8_t i;

    for(i=0; i<BRIDGE_ROUTES_MAX; i++) {
        if (!self->slots[i].in_use) {
            *index = i;
            return true;
        }
    }

    return false;
}

static bool endpointIsNamed(const BridgeEndpoint *endpoint)
{
    return endpoint->agent_name[0] != '\0' && endpoint->interface_name[0] != '\0';
}
