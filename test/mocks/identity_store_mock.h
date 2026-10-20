#pragma once

#include "hub/ports/identity_store_port.h"

#define IDENTITY_STORE_MOCK_ENTRIES_MAX 32

typedef struct {
    IdentityStorePort port;
    char names[IDENTITY_STORE_MOCK_ENTRIES_MAX][64];
    char fingerprints[IDENTITY_STORE_MOCK_ENTRIES_MAX][IDENTITY_FINGERPRINT_HEX_SIZE];
    bool enrolled[IDENTITY_STORE_MOCK_ENTRIES_MAX];
    int entry_count;
    int lookup_calls;
    int pin_calls;
    int enroll_calls;
    int forget_calls;
    int list_calls;
    bool pin_result;
    uint32_t earlier_first_contact_pins;
} IdentityStoreMock;

void IdentityStoreMock_Reset(IdentityStoreMock *self);
void IdentityStoreMock_Preload(IdentityStoreMock *self, const char *agent_name, const char *fingerprint_hex);
