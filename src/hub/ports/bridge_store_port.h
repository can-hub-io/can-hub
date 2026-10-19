#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hub/domain/bridge_routes.h"

/*
 * Outbound contract for the persistence of bridge routes. The broker keeps
 * the live table and loads it once at start, one route per position until
 * load answers false; every admin change is written through before it
 * applies. The platform decides where routes live (SQLite
 * on the hub).
 */
typedef struct {
    void *context;
    bool (*save)(void *context, const BridgeRoute *route);
    bool (*remove)(void *context, const BridgeEndpoint *source, const BridgeEndpoint *destination);
    bool (*load)(void *context, uint16_t position, BridgeRoute *route);
} BridgeStorePort;
