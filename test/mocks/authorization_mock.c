#include "authorization_mock.h"

#include <string.h>

static bool mockWriteAllowed(void *context, const char *fingerprint_hex, const char *agent_name, const char *interface_name);
static bool mockGrant(
    void *context,
    const char *fingerprint_hex,
    const char *agent_name,
    const char *interface_name,
    bool can_write
);
static bool mockRevoke(void *context, const char *fingerprint_hex, const char *agent_name, const char *interface_name);
static uint8_t mockList(void *context, uint16_t offset, AclEntry *entries, uint8_t max_entries, bool *more);
static AclEntry *findEntry(
    AuthorizationMock *self,
    const char *fingerprint_hex,
    const char *agent_name,
    const char *interface_name
);

void AuthorizationMock_Reset(AuthorizationMock *self)
{
    memset(self, 0, sizeof(*self));
    self->port.context = self;
    self->port.write_allowed = mockWriteAllowed;
    self->port.grant = mockGrant;
    self->port.revoke = mockRevoke;
    self->port.list = mockList;
}

void AuthorizationMock_Grant(
    AuthorizationMock *self,
    const char *fingerprint_hex,
    const char *agent_name,
    const char *interface_name,
    bool can_write
)
{
    mockGrant(self, fingerprint_hex, agent_name, interface_name, can_write);
}

static bool mockWriteAllowed(void *context, const char *fingerprint_hex, const char *agent_name, const char *interface_name)
{
    AuthorizationMock *self = context;
    AclEntry *entry;

    self->write_allowed_calls++;
    entry = findEntry(self, fingerprint_hex, agent_name, interface_name);

    return entry != NULL && entry->can_write;
}

static bool mockGrant(
    void *context,
    const char *fingerprint_hex,
    const char *agent_name,
    const char *interface_name,
    bool can_write
)
{
    AuthorizationMock *self = context;
    AclEntry *entry = findEntry(self, fingerprint_hex, agent_name, interface_name);

    if (entry == NULL) {
        if (self->entry_count == AUTHORIZATION_MOCK_ENTRIES_MAX) {
            return false;
        }
        entry = &self->entries[self->entry_count++];
        snprintf(entry->fingerprint_hex, sizeof(entry->fingerprint_hex), "%s", fingerprint_hex);
        snprintf(entry->agent_name, sizeof(entry->agent_name), "%s", agent_name);
        snprintf(entry->interface_name, sizeof(entry->interface_name), "%s", interface_name);
    }
    entry->can_write = can_write;

    return true;
}

static bool mockRevoke(void *context, const char *fingerprint_hex, const char *agent_name, const char *interface_name)
{
    AuthorizationMock *self = context;
    AclEntry *entry = findEntry(self, fingerprint_hex, agent_name, interface_name);
    int index;

    if (entry == NULL) {
        return false;
    }

    index = (int)(entry - self->entries);
    self->entries[index] = self->entries[--self->entry_count];

    return true;
}

static uint8_t mockList(void *context, uint16_t offset, AclEntry *entries, uint8_t max_entries, bool *more)
{
    AuthorizationMock *self = context;
    uint8_t count = 0;
    int i;

    *more = false;
    for(i=offset; i<self->entry_count; i++) {
        if (count == max_entries) {
            *more = true;
            break;
        }
        entries[count++] = self->entries[i];
    }

    return count;
}

static AclEntry *findEntry(
    AuthorizationMock *self,
    const char *fingerprint_hex,
    const char *agent_name,
    const char *interface_name
)
{
    int i;

    for(i=0; i<self->entry_count; i++) {
        if (strcmp(self->entries[i].fingerprint_hex, fingerprint_hex) == 0
            && strcmp(self->entries[i].agent_name, agent_name) == 0
            && strcmp(self->entries[i].interface_name, interface_name) == 0) {
            return &self->entries[i];
        }
    }

    return NULL;
}
