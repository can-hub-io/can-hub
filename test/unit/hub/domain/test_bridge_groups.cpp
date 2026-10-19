#include <cest>

#include <cstdio>
#include <cstring>

extern "C" {
#include "hub/domain/bridge_groups.h"
#include "hub/domain/bridge_targets.h"
#include "protocol/frame_message.h"
}

#define PASSED_ID 0x100
#define OTHER_ID 0x200

static BridgeGroups groups;
static BridgeRoutes routes;
static BridgeTarget targets[BRIDGE_TARGETS_MAX];
static const CanFilter passed_only[] = { { PASSED_ID, FRAME_CAN_ID_MASK } };

static BridgeEndpoint endpoint(const char *agent_name);
static BridgeGroupMember memberOf(const char *agent_name, uint8_t direction);
static void groupOf(const char *name, const char *first, const char *second, const char *third);
static uint16_t targetsFrom(const char *agent_name, uint32_t can_id);
static bool targeted(uint16_t count, const char *agent_name);

describe("bridge_groups", []() {
    beforeEach([]() {
        BridgeGroups_Reset(&groups);
        BridgeRoutes_Reset(&routes);
    });

    it("accepts names of letters, digits, dots, dashes and underscores", []() {
        expect(BridgeGroup_IsNameValid("fleet-2.lab_a")).toBe(true);
        expect(BridgeGroup_IsNameValid("")).toBe(false);
        expect(BridgeGroup_IsNameValid("fleet one")).toBe(false);
        expect(BridgeGroup_IsNameValid("a-name-that-is-thirty-two-chars!")).toBe(false);
    });

    it("keeps the members when a group is set again", []() {
        BridgeGroupMember truck = memberOf("truck42", kBRIDGE_MEMBER_BOTH);

        BridgeGroups_Set(&groups, "fleet", 0, NULL);
        BridgeGroups_Join(&groups, "fleet", &truck);

        BridgeGroups_Set(&groups, "fleet", 1, passed_only);

        expect(BridgeGroups_Find(&groups, "fleet")->group.member_count).toBe(1);
        expect(BridgeGroups_Find(&groups, "fleet")->group.filter_count).toBe(1);
    });

    it("joins a member once however often it joins", []() {
        BridgeGroupMember truck = memberOf("truck42", kBRIDGE_MEMBER_BOTH);

        BridgeGroups_Set(&groups, "fleet", 0, NULL);

        BridgeGroups_Join(&groups, "fleet", &truck);
        BridgeGroups_Join(&groups, "fleet", &truck);

        expect(BridgeGroups_Find(&groups, "fleet")->group.member_count).toBe(1);
    });

    it("refuses a member into a group it does not hold", []() {
        BridgeGroupMember truck = memberOf("truck42", kBRIDGE_MEMBER_BOTH);

        expect(BridgeGroups_Join(&groups, "fleet", &truck)).toBe((uint8_t)kBRIDGE_GROUP_JOIN_UNKNOWN);
    });

    it("refuses a member once the group is full", []() {
        char name[32];
        BridgeGroupMember member;
        BridgeGroupMember extra = memberOf("overflow", kBRIDGE_MEMBER_BOTH);
        uint8_t i;

        BridgeGroups_Set(&groups, "fleet", 0, NULL);
        for(i=0; i<BRIDGE_GROUP_MEMBERS_MAX; i++) {
            snprintf(name, sizeof(name), "bus%u", i);
            member = memberOf(name, kBRIDGE_MEMBER_BOTH);
            BridgeGroups_Join(&groups, "fleet", &member);
        }

        expect(BridgeGroups_Join(&groups, "fleet", &extra)).toBe((uint8_t)kBRIDGE_GROUP_JOIN_FULL);
    });

    it("forgets a member that leaves", []() {
        BridgeEndpoint truck = endpoint("truck42");
        BridgeEndpoint lab = endpoint("lab");

        groupOf("fleet", "truck42", "lab", NULL);

        BridgeGroups_Leave(&groups, "fleet", &truck);

        expect(BridgeGroup_HasMember(&BridgeGroups_Find(&groups, "fleet")->group, &truck)).toBe(false);
        expect(BridgeGroup_HasMember(&BridgeGroups_Find(&groups, "fleet")->group, &lab)).toBe(true);
    });

    it("forgets a removed group", []() {
        groupOf("fleet", "truck42", "lab", NULL);

        BridgeGroups_Remove(&groups, "fleet");

        expect(BridgeGroups_IsEmpty(&groups)).toBe(true);
        expect(targetsFrom("truck42", PASSED_ID)).toBe((uint16_t)0);
    });
});

describe("bridge_targets", []() {
    beforeEach([]() {
        BridgeGroups_Reset(&groups);
        BridgeRoutes_Reset(&routes);
    });

    it("targets every other member of the group of the source", []() {
        uint16_t count;

        groupOf("fleet", "truck42", "lab", "bench");

        count = targetsFrom("truck42", PASSED_ID);

        expect(count).toBe((uint16_t)2);
        expect(targeted(count, "lab")).toBe(true);
        expect(targeted(count, "bench")).toBe(true);
        expect(targeted(count, "truck42")).toBe(false);
    });

    it("targets nothing for a bus outside every group", []() {
        groupOf("fleet", "truck42", "lab", NULL);

        expect(targetsFrom("stranger", PASSED_ID)).toBe((uint16_t)0);
    });

    it("targets only the ids the group filters let through", []() {
        groupOf("fleet", "truck42", "lab", NULL);
        BridgeGroups_Set(&groups, "fleet", 1, passed_only);

        expect(targetsFrom("truck42", PASSED_ID)).toBe((uint16_t)1);
        expect(targetsFrom("truck42", OTHER_ID)).toBe((uint16_t)0);
    });

    it("targets a bus once when a route and a group both reach it", []() {
        BridgeRoute to_lab;

        memset(&to_lab, 0, sizeof(to_lab));
        to_lab.source = endpoint("truck42");
        to_lab.destination = endpoint("lab");
        BridgeRoutes_Set(&routes, &to_lab);
        groupOf("fleet", "truck42", "lab", NULL);

        expect(targetsFrom("truck42", PASSED_ID)).toBe((uint16_t)1);
    });

    it("targets a bus once when two groups both reach it", []() {
        groupOf("fleet", "truck42", "lab", NULL);
        groupOf("diagnostics", "truck42", "lab", "bench");

        expect(targetsFrom("truck42", PASSED_ID)).toBe((uint16_t)2);
    });

    it("feeds nothing from a member that only receives", []() {
        BridgeGroupMember listener = memberOf("lab", kBRIDGE_MEMBER_RECEIVES);

        groupOf("fleet", "truck42", "bench", NULL);
        BridgeGroups_Join(&groups, "fleet", &listener);

        expect(targetsFrom("lab", PASSED_ID)).toBe((uint16_t)0);
        expect(targetsFrom("truck42", PASSED_ID)).toBe((uint16_t)2);
    });

    it("feeds nothing into a member that only sends", []() {
        BridgeGroupMember source = memberOf("lab", kBRIDGE_MEMBER_SENDS);
        uint16_t count;

        groupOf("fleet", "truck42", "bench", NULL);
        BridgeGroups_Join(&groups, "fleet", &source);

        count = targetsFrom("truck42", PASSED_ID);

        expect(count).toBe((uint16_t)1);
        expect(targeted(count, "lab")).toBe(false);
        expect(targetsFrom("lab", PASSED_ID)).toBe((uint16_t)2);
    });

    it("feeds only the ids a member may send", []() {
        BridgeGroupMember filtered = memberOf("lab", kBRIDGE_MEMBER_BOTH);

        filtered.filter_count = 1;
        filtered.filters[0] = passed_only[0];
        groupOf("fleet", "truck42", NULL, NULL);
        BridgeGroups_Join(&groups, "fleet", &filtered);

        expect(targetsFrom("lab", PASSED_ID)).toBe((uint16_t)1);
        expect(targetsFrom("lab", OTHER_ID)).toBe((uint16_t)0);
        expect(targetsFrom("truck42", OTHER_ID)).toBe((uint16_t)1);
    });

    it("changes a member that joins again", []() {
        BridgeGroupMember listener = memberOf("lab", kBRIDGE_MEMBER_RECEIVES);

        groupOf("fleet", "truck42", "lab", NULL);

        BridgeGroups_Join(&groups, "fleet", &listener);

        expect(BridgeGroups_Find(&groups, "fleet")->group.member_count).toBe(2);
        expect(targetsFrom("lab", PASSED_ID)).toBe((uint16_t)0);
    });

    it("never targets anything for an error frame", []() {
        groupOf("fleet", "truck42", "lab", NULL);

        expect(targetsFrom("truck42", FRAME_CAN_ID_FLAG_ERR | PASSED_ID)).toBe((uint16_t)0);
    });
});

static BridgeEndpoint endpoint(const char *agent_name)
{
    BridgeEndpoint built;

    memset(&built, 0, sizeof(built));
    snprintf(built.agent_name, sizeof(built.agent_name), "%s", agent_name);
    snprintf(built.interface_name, sizeof(built.interface_name), "can0");

    return built;
}

static void groupOf(const char *name, const char *first, const char *second, const char *third)
{
    const char *members[] = { first, second, third };
    BridgeGroupMember member;
    uint8_t i;

    BridgeGroups_Set(&groups, name, 0, NULL);
    for(i=0; i<3; i++) {
        if (members[i] != NULL) {
            member = memberOf(members[i], kBRIDGE_MEMBER_BOTH);
            BridgeGroups_Join(&groups, name, &member);
        }
    }
}

static uint16_t targetsFrom(const char *agent_name, uint32_t can_id)
{
    BridgeEndpoint source = endpoint(agent_name);

    return BridgeTargets_Collect(&routes, &groups, &source, can_id, targets);
}

static bool targeted(uint16_t count, const char *agent_name)
{
    uint16_t i;

    for(i=0; i<count; i++) {
        if (strcmp(targets[i].destination->agent_name, agent_name) == 0) {
            return true;
        }
    }

    return false;
}

static BridgeGroupMember memberOf(const char *agent_name, uint8_t direction)
{
    BridgeGroupMember built;

    memset(&built, 0, sizeof(built));
    built.endpoint = endpoint(agent_name);
    built.direction = direction;

    return built;
}
