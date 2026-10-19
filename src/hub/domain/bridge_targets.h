#pragma once

#include <stdint.h>

#include "hub/domain/bridge_groups.h"
#include "hub/domain/bridge_routes.h"

#define BRIDGE_TARGETS_MAX (BRIDGE_ROUTES_MAX + BRIDGE_GROUPS_MAX * BRIDGE_GROUP_MEMBERS_MAX)

/*
 * Where one frame captured on a bus must be injected: the destination of
 * every route out of that bus and every other receiving member of every
 * group it sends into, when the filters of the route, or of the group and
 * of the sending member, let the frame through. A bus is a target
 * once whatever reaches it, and never the bus the frame came from; the
 * counters of the first route or group that reaches it are charged.
 */
typedef struct {
    const BridgeEndpoint *destination;
    uint64_t *forwarded;
    uint64_t *dropped;
} BridgeTarget;

uint16_t BridgeTargets_Collect(
    BridgeRoutes *routes,
    BridgeGroups *groups,
    const BridgeEndpoint *source,
    uint32_t can_id,
    BridgeTarget *targets
);
