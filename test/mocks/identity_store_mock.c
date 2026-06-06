#include "identity_store_mock.h"

#include <stdio.h>
#include <string.h>

static bool mockLookup(void *context, const char *agent_name, char *fingerprint_hex);
static bool mockPin(void *context, const char *agent_name, const char *fingerprint_hex);

void IdentityStoreMock_Reset(IdentityStoreMock *self)
{
    memset(self, 0, sizeof(*self));
    self->port.context = self;
    self->port.lookup = mockLookup;
    self->port.pin = mockPin;
    self->pin_result = true;
}

void IdentityStoreMock_Preload(IdentityStoreMock *self, const char *agent_name, const char *fingerprint_hex)
{
    snprintf(self->names[self->entry_count], sizeof(self->names[0]), "%s", agent_name);
    snprintf(self->fingerprints[self->entry_count], sizeof(self->fingerprints[0]), "%s", fingerprint_hex);
    self->entry_count++;
}

static bool mockLookup(void *context, const char *agent_name, char *fingerprint_hex)
{
    IdentityStoreMock *self = context;
    int i;

    self->lookup_calls++;
    for(i=0; i<self->entry_count; i++) {
        if (strcmp(self->names[i], agent_name) == 0) {
            snprintf(fingerprint_hex, IDENTITY_FINGERPRINT_HEX_SIZE, "%s", self->fingerprints[i]);
            return true;
        }
    }

    return false;
}

static bool mockPin(void *context, const char *agent_name, const char *fingerprint_hex)
{
    IdentityStoreMock *self = context;

    self->pin_calls++;
    if (!self->pin_result) {
        return false;
    }

    IdentityStoreMock_Preload(self, agent_name, fingerprint_hex);

    return true;
}
