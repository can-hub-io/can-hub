#pragma once

#include <stdbool.h>

#include <sqlite3.h>

#include "hub/ports/identity_store_port.h"

/*
 * SQLite-backed agent identity store for the hub: pins of agent_name to
 * TLS fingerprint live in the agent_identities table. Implements
 * IdentityStorePort; replaces the known_agents text file, which can be
 * imported once on first start.
 */
typedef struct {
    IdentityStorePort port;
    sqlite3 *database;
} IdentityDatabase;

bool IdentityDatabase_Open(IdentityDatabase *self, const char *path);
void IdentityDatabase_Close(IdentityDatabase *self);
IdentityStorePort *IdentityDatabase_Port(IdentityDatabase *self);
bool IdentityDatabase_ImportPinFile(IdentityDatabase *self, const char *path);
