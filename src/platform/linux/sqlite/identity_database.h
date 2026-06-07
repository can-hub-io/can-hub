#pragma once

#include <stdbool.h>

#include <sqlite3.h>

#include "hub/ports/authorization_port.h"
#include "hub/ports/identity_store_port.h"

/*
 * SQLite-backed hub store: agent identity pins (agent_name -> TLS
 * fingerprint, agent_identities table) and client write ACLs (client
 * fingerprint -> namespaced interface, client_acls table). One hub.db, one
 * connection; implements IdentityStorePort and AuthorizationPort.
 */
typedef struct {
    IdentityStorePort identity_port;
    AuthorizationPort authorization_port;
    sqlite3 *database;
} IdentityDatabase;

bool IdentityDatabase_Open(IdentityDatabase *self, const char *path);
void IdentityDatabase_Close(IdentityDatabase *self);
IdentityStorePort *IdentityDatabase_Port(IdentityDatabase *self);
AuthorizationPort *IdentityDatabase_AuthorizationPort(IdentityDatabase *self);
bool IdentityDatabase_ImportPinFile(IdentityDatabase *self, const char *path);
