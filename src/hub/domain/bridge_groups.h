#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hub/domain/bridge_routes.h"

#define BRIDGE_GROUPS_MAX 16
#define BRIDGE_GROUP_MEMBERS_MAX 16
#define BRIDGE_GROUP_NAME_SIZE REGISTER_INTERFACE_NAME_SIZE
#define BRIDGE_GROUP_AGENT_NAME "group"
#define BRIDGE_MEMBER_FILTERS_MAX 8

/*
 * Bridge groups: buses that behave as one. A frame captured on a member that
 * passes the group filters is injected into every other member, never back
 * into the one it came from. Members are named interfaces, like route
 * endpoints, so a group outlives the agents that serve it. Setting a group
 * again replaces its filters and keeps its members. A name is 1-15 of
 * [A-Za-z0-9._-], so that the group is also an interface clients open as
 * group/<name> under the reserved agent name "group". Each member has a
 * direction: both (the default), sends (its frames go to the group, it
 * receives nothing) or receives (it gets the group's frames, its own stay
 * home); and filters on what it sends, applied together with the group ones.
 * Joining a member again replaces its direction and filters.
 */

typedef enum tbridge_member_direction_e {
    kBRIDGE_MEMBER_BOTH = 0,
    kBRIDGE_MEMBER_SENDS,
    kBRIDGE_MEMBER_RECEIVES,
    kBRIDGE_MEMBER_MAX,
} TBRIDGE_MEMBER_DIRECTION;
typedef enum tbridge_group_join_e {
    kBRIDGE_GROUP_JOIN_OK = 0,
    kBRIDGE_GROUP_JOIN_UNKNOWN,
    kBRIDGE_GROUP_JOIN_FULL,
    kBRIDGE_GROUP_JOIN_MAX,
} TBRIDGE_GROUP_JOIN;

typedef struct {
    BridgeEndpoint endpoint;
    uint8_t direction;
    uint8_t filter_count;
    CanFilter filters[BRIDGE_MEMBER_FILTERS_MAX];
} BridgeGroupMember;

typedef struct {
    char name[BRIDGE_GROUP_NAME_SIZE];
    uint8_t filter_count;
    CanFilter filters[BRIDGE_FILTERS_MAX];
    uint8_t member_count;
    BridgeGroupMember members[BRIDGE_GROUP_MEMBERS_MAX];
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
bool BridgeGroup_Carries(const BridgeGroup *group, const BridgeEndpoint *source, uint32_t can_id);
bool BridgeGroupMember_IsValid(const BridgeGroupMember *member);
bool BridgeGroupMember_Receives(const BridgeGroupMember *member);

void BridgeGroups_Reset(BridgeGroups *self);
bool BridgeGroups_IsEmpty(const BridgeGroups *self);
bool BridgeGroups_HasRoom(const BridgeGroups *self, const char *name);
bool BridgeGroups_Set(BridgeGroups *self, const char *name, uint8_t filter_count, const CanFilter *filters);
bool BridgeGroups_Remove(BridgeGroups *self, const char *name);
uint8_t BridgeGroups_Join(BridgeGroups *self, const char *name, const BridgeGroupMember *member);
bool BridgeGroups_Leave(BridgeGroups *self, const char *name, const BridgeEndpoint *member);
bool BridgeGroups_Restore(BridgeGroups *self, const BridgeGroup *group);
BridgeGroupSlot *BridgeGroups_Find(BridgeGroups *self, const char *name);
const BridgeGroupSlot *BridgeGroups_Nth(const BridgeGroups *self, uint16_t position);
BridgeGroupSlot *BridgeGroups_Slot(BridgeGroups *self, uint8_t slot_index);
