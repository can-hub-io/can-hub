#include "hub/domain/bridge_groups.h"

#include <string.h>

static bool findIndex(const BridgeGroups *self, const char *name, uint8_t *index);
static bool freeIndex(const BridgeGroups *self, uint8_t *index);
static bool isNameCharacter(char character);
static bool memberIndex(const BridgeGroup *group, const BridgeEndpoint *member, uint8_t *index);

/* ---------- public ---------- */

bool BridgeGroup_IsNameValid(const char *name)
{
    uint8_t i;

    if (name[0] == '\0') {
        return false;
    }

    for(i=0; i<BRIDGE_GROUP_NAME_SIZE; i++) {
        if (name[i] == '\0') {
            return true;
        }
        if (!isNameCharacter(name[i])) {
            return false;
        }
    }

    return false;
}

bool BridgeGroup_HasMember(const BridgeGroup *group, const BridgeEndpoint *member)
{
    uint8_t index;

    return memberIndex(group, member, &index);
}

void BridgeGroups_Reset(BridgeGroups *self)
{
    memset(self, 0, sizeof(*self));
}

bool BridgeGroups_IsEmpty(const BridgeGroups *self)
{
    return self->count == 0;
}

bool BridgeGroups_HasRoom(const BridgeGroups *self, const char *name)
{
    uint8_t index;

    return findIndex(self, name, &index) || freeIndex(self, &index);
}

bool BridgeGroups_Set(BridgeGroups *self, const char *name, uint8_t filter_count, const CanFilter *filters)
{
    BridgeGroupSlot *slot;
    uint8_t index;

    if (!BridgeGroup_IsNameValid(name) || filter_count > BRIDGE_FILTERS_MAX) {
        return false;
    }
    if (!findIndex(self, name, &index) && !freeIndex(self, &index)) {
        return false;
    }

    slot = &self->slots[index];
    if (!slot->in_use) {
        memset(slot, 0, sizeof(*slot));
        slot->in_use = true;
        memcpy(slot->group.name, name, strlen(name));
        self->count++;
    }
    slot->group.filter_count = filter_count;
    if (filter_count > 0) {
        memcpy(slot->group.filters, filters, (size_t)filter_count * sizeof(filters[0]));
    }

    return true;
}

bool BridgeGroups_Remove(BridgeGroups *self, const char *name)
{
    uint8_t index;

    if (!findIndex(self, name, &index)) {
        return false;
    }

    memset(&self->slots[index], 0, sizeof(self->slots[index]));
    self->count--;

    return true;
}

uint8_t BridgeGroups_Join(BridgeGroups *self, const char *name, const BridgeEndpoint *member)
{
    BridgeGroup *group;
    uint8_t index;

    if (!findIndex(self, name, &index) || !BridgeEndpoint_IsNamed(member)) {
        return kBRIDGE_GROUP_JOIN_UNKNOWN;
    }

    group = &self->slots[index].group;
    if (BridgeGroup_HasMember(group, member)) {
        return kBRIDGE_GROUP_JOIN_OK;
    }
    if (group->member_count == BRIDGE_GROUP_MEMBERS_MAX) {
        return kBRIDGE_GROUP_JOIN_FULL;
    }

    group->members[group->member_count++] = *member;

    return kBRIDGE_GROUP_JOIN_OK;
}

bool BridgeGroups_Leave(BridgeGroups *self, const char *name, const BridgeEndpoint *member)
{
    BridgeGroup *group;
    uint8_t group_index;
    uint8_t index;

    if (!findIndex(self, name, &group_index)) {
        return false;
    }

    group = &self->slots[group_index].group;
    if (!memberIndex(group, member, &index)) {
        return false;
    }

    group->member_count--;
    group->members[index] = group->members[group->member_count];
    memset(&group->members[group->member_count], 0, sizeof(group->members[0]));

    return true;
}

bool BridgeGroups_Restore(BridgeGroups *self, const BridgeGroup *group)
{
    uint8_t i;

    if (!BridgeGroups_Set(self, group->name, group->filter_count, group->filters)) {
        return false;
    }

    for(i=0; i<group->member_count && i<BRIDGE_GROUP_MEMBERS_MAX; i++) {
        BridgeGroups_Join(self, group->name, &group->members[i]);
    }

    return true;
}

BridgeGroupSlot *BridgeGroups_Find(BridgeGroups *self, const char *name)
{
    uint8_t index;

    if (!findIndex(self, name, &index)) {
        return NULL;
    }

    return &self->slots[index];
}

const BridgeGroupSlot *BridgeGroups_Nth(const BridgeGroups *self, uint16_t position)
{
    uint16_t seen = 0;
    uint8_t i;

    for(i=0; i<BRIDGE_GROUPS_MAX; i++) {
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

BridgeGroupSlot *BridgeGroups_Slot(BridgeGroups *self, uint8_t slot_index)
{
    if (slot_index >= BRIDGE_GROUPS_MAX || !self->slots[slot_index].in_use) {
        return NULL;
    }

    return &self->slots[slot_index];
}

/* ---------- private ---------- */

static bool findIndex(const BridgeGroups *self, const char *name, uint8_t *index)
{
    uint8_t i;

    for(i=0; i<BRIDGE_GROUPS_MAX; i++) {
        if (self->slots[i].in_use && strcmp(self->slots[i].group.name, name) == 0) {
            *index = i;
            return true;
        }
    }

    return false;
}

static bool freeIndex(const BridgeGroups *self, uint8_t *index)
{
    uint8_t i;

    for(i=0; i<BRIDGE_GROUPS_MAX; i++) {
        if (!self->slots[i].in_use) {
            *index = i;
            return true;
        }
    }

    return false;
}

static bool isNameCharacter(char character)
{
    if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z')) {
        return true;
    }
    if (character >= '0' && character <= '9') {
        return true;
    }

    return character == '.' || character == '_' || character == '-';
}

static bool memberIndex(const BridgeGroup *group, const BridgeEndpoint *member, uint8_t *index)
{
    uint8_t i;

    for(i=0; i<group->member_count; i++) {
        if (BridgeEndpoint_Equals(&group->members[i], member)) {
            *index = i;
            return true;
        }
    }

    return false;
}
