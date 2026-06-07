#pragma once

#include "hub/ports/authorization_port.h"

#define AUTHORIZATION_MOCK_ENTRIES_MAX 8

typedef struct {
    AuthorizationPort port;
    AclEntry entries[AUTHORIZATION_MOCK_ENTRIES_MAX];
    int entry_count;
    int write_allowed_calls;
} AuthorizationMock;

void AuthorizationMock_Reset(AuthorizationMock *self);
void AuthorizationMock_Grant(
    AuthorizationMock *self,
    const char *fingerprint_hex,
    const char *agent_name,
    const char *interface_name,
    bool can_read,
    bool can_write
);
