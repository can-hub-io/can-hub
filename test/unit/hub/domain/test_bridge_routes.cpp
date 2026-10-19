#include <cest>

#include <cstdio>
#include <cstring>

extern "C" {
#include "hub/domain/bridge_routes.h"
#include "protocol/frame_message.h"
}

#define PASSED_ID 0x100
#define OTHER_ID 0x200
#define RANGE_MASK 0x700

static BridgeRoutes routes;
static uint8_t slot_indices[BRIDGE_ROUTES_MAX];

static BridgeRoute route(const char *source_agent, const char *destination_agent);
static uint8_t matching(const char *agent_name, uint32_t can_id);

describe("bridge_routes", []() {
    beforeEach([]() {
        BridgeRoutes_Reset(&routes);
    });

    it("matches a frame of the source bus", []() {
        BridgeRoute truck_to_bus = route("truck42", "bus7");

        BridgeRoutes_Set(&routes, &truck_to_bus);

        expect(matching("truck42", PASSED_ID)).toBe(1);
        expect(matching("bus7", PASSED_ID)).toBe(0);
    });

    it("matches only the ids its filters name", []() {
        BridgeRoute filtered = route("truck42", "bus7");

        filtered.filter_count = 1;
        filtered.filters[0] = { PASSED_ID, FRAME_CAN_ID_MASK };
        BridgeRoutes_Set(&routes, &filtered);

        expect(matching("truck42", PASSED_ID)).toBe(1);
        expect(matching("truck42", OTHER_ID)).toBe(0);
    });

    it("matches a range through the mask", []() {
        BridgeRoute ranged = route("truck42", "bus7");

        ranged.filter_count = 1;
        ranged.filters[0] = { PASSED_ID, RANGE_MASK };
        BridgeRoutes_Set(&routes, &ranged);

        expect(matching("truck42", PASSED_ID + 0x7F)).toBe(1);
        expect(matching("truck42", OTHER_ID)).toBe(0);
    });

    it("never matches an error frame", []() {
        BridgeRoute truck_to_bus = route("truck42", "bus7");

        BridgeRoutes_Set(&routes, &truck_to_bus);

        expect(matching("truck42", FRAME_CAN_ID_FLAG_ERR | PASSED_ID)).toBe(0);
    });

    it("matches every route out of the same bus", []() {
        BridgeRoute to_bus = route("truck42", "bus7");
        BridgeRoute to_lab = route("truck42", "lab1");

        BridgeRoutes_Set(&routes, &to_bus);
        BridgeRoutes_Set(&routes, &to_lab);

        expect(matching("truck42", PASSED_ID)).toBe(2);
    });

    it("keeps one route per pair of buses and its counters", []() {
        BridgeRoute truck_to_bus = route("truck42", "bus7");

        BridgeRoutes_Set(&routes, &truck_to_bus);
        BridgeRoutes_Slot(&routes, 0)->forwarded = 5;
        truck_to_bus.filter_count = 1;

        BridgeRoutes_Set(&routes, &truck_to_bus);

        expect(BridgeRoutes_Nth(&routes, 1) == nullptr).toBe(true);
        expect(BridgeRoutes_Nth(&routes, 0)->route.filter_count).toBe(1);
        expect(BridgeRoutes_Nth(&routes, 0)->forwarded).toBe((uint64_t)5);
    });

    it("refuses a route from a bus into itself", []() {
        BridgeRoute loop = route("truck42", "truck42");

        expect(BridgeRoutes_Set(&routes, &loop)).toBe(false);
    });

    it("refuses a route once the table is full", []() {
        BridgeRoute extra = route("truck42", "overflow");
        char destination[32];
        BridgeRoute filler;
        uint8_t i;

        for(i=0; i<BRIDGE_ROUTES_MAX; i++) {
            snprintf(destination, sizeof(destination), "bus%u", i);
            filler = route("truck42", destination);
            BridgeRoutes_Set(&routes, &filler);
        }

        expect(BridgeRoutes_HasRoom(&routes, &extra)).toBe(false);
        expect(BridgeRoutes_Set(&routes, &extra)).toBe(false);
        expect(BridgeRoutes_HasRoom(&routes, &filler)).toBe(true);
    });

    it("forgets a removed route", []() {
        BridgeRoute truck_to_bus = route("truck42", "bus7");

        BridgeRoutes_Set(&routes, &truck_to_bus);

        BridgeRoutes_Remove(&routes, &truck_to_bus.source, &truck_to_bus.destination);

        expect(matching("truck42", PASSED_ID)).toBe(0);
        expect(BridgeRoutes_IsEmpty(&routes)).toBe(true);
        expect(BridgeRoutes_Find(&routes, &truck_to_bus.source, &truck_to_bus.destination) == nullptr).toBe(true);
    });
});

static BridgeRoute route(const char *source_agent, const char *destination_agent)
{
    BridgeRoute built;

    memset(&built, 0, sizeof(built));
    snprintf(built.source.agent_name, sizeof(built.source.agent_name), "%s", source_agent);
    snprintf(built.source.interface_name, sizeof(built.source.interface_name), "can0");
    snprintf(built.destination.agent_name, sizeof(built.destination.agent_name), "%s", destination_agent);
    snprintf(built.destination.interface_name, sizeof(built.destination.interface_name), "can0");

    return built;
}

static uint8_t matching(const char *agent_name, uint32_t can_id)
{
    return BridgeRoutes_Matching(&routes, agent_name, "can0", can_id, slot_indices);
}
