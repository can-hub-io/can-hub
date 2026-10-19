#pragma once

#include <stdbool.h>

#include <sqlite3.h>

#include "hub/ports/authorization_port.h"
#include "hub/ports/bridge_store_port.h"
#include "hub/ports/identity_store_port.h"

/*
 * SQLite-backed hub store: agent identity pins (agent_name -> TLS
 * fingerprint, agent_identities table) and client write ACLs (client
 * fingerprint -> namespaced interface, client_acls table) and bus-to-bus
 * routes (bridge_routes table, filters as a blob of little-endian id/mask
 * pairs). One hub.db, one connection; implements IdentityStorePort,
 * AuthorizationPort and BridgeStorePort.
 */
typedef struct {
    IdentityStorePort identity_port;
    AuthorizationPort authorization_port;
    BridgeStorePort bridge_port;
    sqlite3 *database;
} IdentityDatabase;

bool IdentityDatabase_Open(IdentityDatabase *self, const char *path);
void IdentityDatabase_Close(IdentityDatabase *self);
IdentityStorePort *IdentityDatabase_Port(IdentityDatabase *self);
AuthorizationPort *IdentityDatabase_AuthorizationPort(IdentityDatabase *self);
BridgeStorePort *IdentityDatabase_BridgePort(IdentityDatabase *self);
bool IdentityDatabase_ImportPinFile(IdentityDatabase *self, const char *path);
