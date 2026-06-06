#pragma once

#include "hub/ports/identity_store_port.h"

#define IDENTITY_STORE_MOCK_ENTRIES_MAX 8

typedef struct {
    IdentityStorePort port;
    char names[IDENTITY_STORE_MOCK_ENTRIES_MAX][64];
    char fingerprints[IDENTITY_STORE_MOCK_ENTRIES_MAX][IDENTITY_FINGERPRINT_HEX_SIZE];
    int entry_count;
    int lookup_calls;
    int pin_calls;
    bool pin_result;
} IdentityStoreMock;

void IdentityStoreMock_Reset(IdentityStoreMock *self);
void IdentityStoreMock_Preload(IdentityStoreMock *self, const char *agent_name, const char *fingerprint_hex);
