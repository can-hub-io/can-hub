#include "authorization_mock.h"

#include <string.h>

static bool mockReadAllowed(void *context, const char *fingerprint_hex, const char *agent_name, const char *interface_name);
static bool mockWriteAllowed(void *context, const char *fingerprint_hex, const char *agent_name, const char *interface_name);
static bool mockGrant(
    void *context,
    const char *fingerprint_hex,
    const char *agent_name,
    const char *interface_name,
    bool can_read,
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
static const AclEntry *resolve(
    AuthorizationMock *self,
    const char *fingerprint_hex,
    const char *agent_name,
    const char *interface_name
);
static bool patternMatches(const char *pattern, const char *value);
static int precedence(const AclEntry *entry);

void AuthorizationMock_Reset(AuthorizationMock *self)
{
    memset(self, 0, sizeof(*self));
    self->port.context = self;
    self->port.read_allowed = mockReadAllowed;
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
    bool can_read,
    bool can_write
)
{
    mockGrant(self, fingerprint_hex, agent_name, interface_name, can_read, can_write);
}

static bool mockReadAllowed(void *context, const char *fingerprint_hex, const char *agent_name, const char *interface_name)
{
    const AclEntry *entry = resolve(context, fingerprint_hex, agent_name, interface_name);

    return entry == NULL ? true : entry->can_read;
}

static bool mockWriteAllowed(void *context, const char *fingerprint_hex, const char *agent_name, const char *interface_name)
{
    const AclEntry *entry = resolve(context, fingerprint_hex, agent_name, interface_name);

    return entry == NULL ? false : entry->can_write;
}

static bool mockGrant(
    void *context,
    const char *fingerprint_hex,
    const char *agent_name,
    const char *interface_name,
    bool can_read,
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
    entry->can_read = can_read;
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

static const AclEntry *resolve(
    AuthorizationMock *self,
    const char *fingerprint_hex,
    const char *agent_name,
    const char *interface_name
)
{
    const AclEntry *best = NULL;
    int best_precedence = -1;
    int i;

    for(i=0; i<self->entry_count; i++) {
        if (!patternMatches(self->entries[i].fingerprint_hex, fingerprint_hex)
            || !patternMatches(self->entries[i].agent_name, agent_name)
            || !patternMatches(self->entries[i].interface_name, interface_name)) {
            continue;
        }
        if (precedence(&self->entries[i]) > best_precedence) {
            best_precedence = precedence(&self->entries[i]);
            best = &self->entries[i];
        }
    }

    return best;
}

static bool patternMatches(const char *pattern, const char *value)
{
    return strcmp(pattern, AUTHORIZATION_WILDCARD) == 0 || strcmp(pattern, value) == 0;
}

static int precedence(const AclEntry *entry)
{
    int subject_rank = 0;
    int object_rank = 0;

    if (strcmp(entry->fingerprint_hex, AUTHORIZATION_WILDCARD) != 0) {
        subject_rank = 1;
    }
    if (strcmp(entry->agent_name, AUTHORIZATION_WILDCARD) != 0) {
        object_rank += 2;
    }
    if (strcmp(entry->interface_name, AUTHORIZATION_WILDCARD) != 0) {
        object_rank += 1;
    }

    return subject_rank * 4 + object_rank;
}
