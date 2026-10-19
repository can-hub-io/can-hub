#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hub/domain/bridge_routes.h"

#define BRIDGE_GROUPS_MAX 16
#define BRIDGE_GROUP_MEMBERS_MAX 16
#define BRIDGE_GROUP_NAME_SIZE REGISTER_INTERFACE_NAME_SIZE
#define BRIDGE_GROUP_AGENT_NAME "group"

/*
 * Bridge groups: buses that behave as one. A frame captured on a member that
 * passes the group filters is injected into every other member, never back
 * into the one it came from. Members are named interfaces, like route
 * endpoints, so a group outlives the agents that serve it. Setting a group
 * again replaces its filters and keeps its members. A name is 1-15 of
 * [A-Za-z0-9._-], so that the group is also an interface clients open as
 * group/<name> under the reserved agent name "group".
 */
typedef enum tbridge_group_join_e {
    kBRIDGE_GROUP_JOIN_OK = 0,
    kBRIDGE_GROUP_JOIN_UNKNOWN,
    kBRIDGE_GROUP_JOIN_FULL,
    kBRIDGE_GROUP_JOIN_MAX,
} TBRIDGE_GROUP_JOIN;

typedef struct {
    char name[BRIDGE_GROUP_NAME_SIZE];
    uint8_t filter_count;
    CanFilter filters[BRIDGE_FILTERS_MAX];
    uint8_t member_count;
    BridgeEndpoint members[BRIDGE_GROUP_MEMBERS_MAX];
} BridgeGroup;

typedef struct {
    bool in_use;
    BridgeGroup group;
    uint64_t forwarded;
    uint64_t dropped;
} BridgeGroupSlot;

typedef struct {
    BridgeGroupSlot slots[BRIDGE_GROUPS_MAX];
    uint8_t count;
} BridgeGroups;

bool BridgeGroup_IsNameValid(const char *name);
bool BridgeGroup_HasMember(const BridgeGroup *group, const BridgeEndpoint *member);

void BridgeGroups_Reset(BridgeGroups *self);
bool BridgeGroups_IsEmpty(const BridgeGroups *self);
bool BridgeGroups_HasRoom(const BridgeGroups *self, const char *name);
bool BridgeGroups_Set(BridgeGroups *self, const char *name, uint8_t filter_count, const CanFilter *filters);
bool BridgeGroups_Remove(BridgeGroups *self, const char *name);
uint8_t BridgeGroups_Join(BridgeGroups *self, const char *name, const BridgeEndpoint *member);
bool BridgeGroups_Leave(BridgeGroups *self, const char *name, const BridgeEndpoint *member);
bool BridgeGroups_Restore(BridgeGroups *self, const BridgeGroup *group);
BridgeGroupSlot *BridgeGroups_Find(BridgeGroups *self, const char *name);
const BridgeGroupSlot *BridgeGroups_Nth(const BridgeGroups *self, uint16_t position);
BridgeGroupSlot *BridgeGroups_Slot(BridgeGroups *self, uint8_t slot_index);
