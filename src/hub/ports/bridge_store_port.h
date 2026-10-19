#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hub/domain/bridge_groups.h"
#include "hub/domain/bridge_routes.h"

/*
 * Outbound contract for the persistence of bridge routes and groups. The
 * broker keeps the live tables and loads them once at start, one route or
 * group (members included) per position until load answers false; every
 * admin change is written through before it applies. save_group stores the
 * name and filters only; members are added (or updated) and removed one at
 * a time. The platform decides where routes live (SQLite
 * on the hub).
 */
typedef struct {
    void *context;
    bool (*save)(void *context, const BridgeRoute *route);
    bool (*remove)(void *context, const BridgeEndpoint *source, const BridgeEndpoint *destination);
    bool (*load)(void *context, uint16_t position, BridgeRoute *route);
    bool (*save_group)(void *context, const BridgeGroup *group);
    bool (*remove_group)(void *context, const char *name);
    bool (*add_member)(void *context, const char *name, const BridgeGroupMember *member);
    bool (*remove_member)(void *context, const char *name, const BridgeEndpoint *member);
    bool (*load_group)(void *context, uint16_t position, BridgeGroup *group);
} BridgeStorePort;
