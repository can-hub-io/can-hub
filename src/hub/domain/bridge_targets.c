#include "hub/domain/bridge_targets.h"

static uint16_t collectRoutes(BridgeRoutes *routes, const BridgeEndpoint *source, uint32_t can_id, BridgeTarget *targets, uint16_t count);
static uint16_t collectGroups(BridgeGroups *groups, const BridgeEndpoint *source, uint32_t can_id, BridgeTarget *targets, uint16_t count);
static uint16_t addTarget(
    BridgeTarget *targets,
    uint16_t count,
    const BridgeEndpoint *source,
    const BridgeEndpoint *destination,
    uint64_t *forwarded,
    uint64_t *dropped
);

/* ---------- public ---------- */

uint16_t BridgeTargets_Collect(
    BridgeRoutes *routes,
    BridgeGroups *groups,
    const BridgeEndpoint *source,
    uint32_t can_id,
    BridgeTarget *targets
)
{
    uint16_t count = collectRoutes(routes, source, can_id, targets, 0);

    return collectGroups(groups, source, can_id, targets, count);
}

/* ---------- private ---------- */

static uint16_t collectRoutes(BridgeRoutes *routes, const BridgeEndpoint *source, uint32_t can_id, BridgeTarget *targets, uint16_t count)
{
    BridgeRouteSlot *slot;
    uint8_t i;

    for(i=0; i<BRIDGE_ROUTES_MAX; i++) {
        slot = BridgeRoutes_Slot(routes, i);
        if (slot == NULL || !BridgeEndpoint_Equals(&slot->route.source, source)) {
            continue;
        }
        if (BridgeRoute_Passes(&slot->route, can_id)) {
            count = addTarget(targets, count, source, &slot->route.destination, &slot->forwarded, &slot->dropped);
        }
    }

    return count;
}

static uint16_t collectGroups(BridgeGroups *groups, const BridgeEndpoint *source, uint32_t can_id, BridgeTarget *targets, uint16_t count)
{
    BridgeGroupSlot *slot;
    uint8_t i;
    uint8_t m;

    for(i=0; i<BRIDGE_GROUPS_MAX; i++) {
        slot = BridgeGroups_Slot(groups, i);
        if (slot == NULL || !BridgeGroup_HasMember(&slot->group, source)) {
            continue;
        }
        if (!BridgeFilters_Pass(slot->group.filters, slot->group.filter_count, can_id)) {
            continue;
        }
        for(m=0; m<slot->group.member_count; m++) {
            count = addTarget(targets, count, source, &slot->group.members[m], &slot->forwarded, &slot->dropped);
        }
    }

    return count;
}

static uint16_t addTarget(
    BridgeTarget *targets,
    uint16_t count,
    const BridgeEndpoint *source,
    const BridgeEndpoint *destination,
    uint64_t *forwarded,
    uint64_t *dropped
)
{
    uint16_t i;

    if (BridgeEndpoint_Equals(destination, source) || count == BRIDGE_TARGETS_MAX) {
        return count;
    }
    for(i=0; i<count; i++) {
        if (BridgeEndpoint_Equals(targets[i].destination, destination)) {
            return count;
        }
    }

    targets[count].destination = destination;
    targets[count].forwarded = forwarded;
    targets[count].dropped = dropped;

    return (uint16_t)(count + 1);
}
