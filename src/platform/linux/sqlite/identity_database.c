#include "platform/linux/sqlite/identity_database.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LINE_BUFFER_SIZE 512
#define IMPORT_KEY_MAX 256

#define SCHEMA \
    "CREATE TABLE IF NOT EXISTS agent_identities (" \
    "name TEXT PRIMARY KEY," \
    "fingerprint TEXT NOT NULL," \
    "first_seen_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))" \
    ");" \
    "CREATE TABLE IF NOT EXISTS client_acls (" \
    "fingerprint TEXT NOT NULL," \
    "agent_name TEXT NOT NULL," \
    "interface_name TEXT NOT NULL," \
    "can_write INTEGER NOT NULL DEFAULT 0," \
    "PRIMARY KEY (fingerprint, agent_name, interface_name)" \
    ")"

static bool storeLookup(void *context, const char *agent_name, char *fingerprint_hex);
static bool storePin(void *context, const char *agent_name, const char *fingerprint_hex);
static bool storeForget(void *context, const char *agent_name);
static uint8_t storeList(void *context, uint16_t offset, IdentityPin *pins, uint8_t max_pins, bool *more);
static bool insertPin(IdentityDatabase *self, const char *sql, const char *agent_name, const char *fingerprint_hex);
static bool aclWriteAllowed(void *context, const char *fingerprint_hex, const char *agent_name, const char *interface_name);
static bool aclGrant(
    void *context,
    const char *fingerprint_hex,
    const char *agent_name,
    const char *interface_name,
    bool can_write
);
static bool aclRevoke(void *context, const char *fingerprint_hex, const char *agent_name, const char *interface_name);
static uint8_t aclList(void *context, uint16_t offset, AclEntry *entries, uint8_t max_entries, bool *more);

/* ---------- public ---------- */

bool IdentityDatabase_Open(IdentityDatabase *self, const char *path)
{
    memset(self, 0, sizeof(*self));
    self->identity_port.context = self;
    self->identity_port.lookup = storeLookup;
    self->identity_port.pin = storePin;
    self->identity_port.forget = storeForget;
    self->identity_port.list = storeList;
    self->authorization_port.context = self;
    self->authorization_port.write_allowed = aclWriteAllowed;
    self->authorization_port.grant = aclGrant;
    self->authorization_port.revoke = aclRevoke;
    self->authorization_port.list = aclList;

    if (sqlite3_open(path, &self->database) != SQLITE_OK) {
        IdentityDatabase_Close(self);
        return false;
    }
    if (sqlite3_exec(self->database, SCHEMA, NULL, NULL, NULL) != SQLITE_OK) {
        IdentityDatabase_Close(self);
        return false;
    }

    return true;
}

void IdentityDatabase_Close(IdentityDatabase *self)
{
    if (self->database != NULL) {
        sqlite3_close(self->database);
        self->database = NULL;
    }
}

IdentityStorePort *IdentityDatabase_Port(IdentityDatabase *self)
{
    return &self->identity_port;
}

AuthorizationPort *IdentityDatabase_AuthorizationPort(IdentityDatabase *self)
{
    return &self->authorization_port;
}

bool IdentityDatabase_ImportPinFile(IdentityDatabase *self, const char *path)
{
    char line[LINE_BUFFER_SIZE];
    char name[IMPORT_KEY_MAX];
    char fingerprint[IDENTITY_FINGERPRINT_HEX_SIZE];
    FILE *file = fopen(path, "r");
    bool imported = true;

    if (file == NULL) {
        return false;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        if (sscanf(line, "%255s %64s", name, fingerprint) != 2) {
            continue;
        }
        if (!insertPin(self, "INSERT OR IGNORE INTO agent_identities (name, fingerprint) VALUES (?1, ?2)", name, fingerprint)) {
            imported = false;
            break;
        }
    }

    fclose(file);

    return imported;
}

/* ---------- private ---------- */

static bool storeLookup(void *context, const char *agent_name, char *fingerprint_hex)
{
    IdentityDatabase *self = context;
    sqlite3_stmt *statement = NULL;
    const uint8_t *stored;
    bool found = false;
    int32_t prepare_result;

    prepare_result = sqlite3_prepare_v2(
        self->database,
        "SELECT fingerprint FROM agent_identities WHERE name = ?1",
        -1,
        &statement,
        NULL
    );
    if (prepare_result != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(statement, 1, agent_name, -1, SQLITE_STATIC);
    if (sqlite3_step(statement) == SQLITE_ROW) {
        stored = sqlite3_column_text(statement, 0);
        snprintf(fingerprint_hex, IDENTITY_FINGERPRINT_HEX_SIZE, "%s", (const char *)stored);
        found = true;
    }
    sqlite3_finalize(statement);

    return found;
}

static bool storePin(void *context, const char *agent_name, const char *fingerprint_hex)
{
    IdentityDatabase *self = context;
    bool pinned;

    pinned = insertPin(
        self,
        "INSERT INTO agent_identities (name, fingerprint) VALUES (?1, ?2)",
        agent_name,
        fingerprint_hex
    );
    if (pinned) {
        fprintf(stderr, "pinning agent %s fingerprint %s\n", agent_name, fingerprint_hex);
    }

    return pinned;
}

static bool storeForget(void *context, const char *agent_name)
{
    IdentityDatabase *self = context;
    sqlite3_stmt *statement = NULL;
    bool forgotten = false;

    if (sqlite3_prepare_v2(self->database, "DELETE FROM agent_identities WHERE name = ?1", -1, &statement, NULL) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(statement, 1, agent_name, -1, SQLITE_STATIC);
    if (sqlite3_step(statement) == SQLITE_DONE) {
        forgotten = sqlite3_changes(self->database) > 0;
    }
    sqlite3_finalize(statement);

    return forgotten;
}

static uint8_t storeList(void *context, uint16_t offset, IdentityPin *pins, uint8_t max_pins, bool *more)
{
    IdentityDatabase *self = context;
    sqlite3_stmt *statement = NULL;
    uint8_t count = 0;
    int32_t prepare_result;

    *more = false;
    prepare_result = sqlite3_prepare_v2(
        self->database,
        "SELECT name, fingerprint FROM agent_identities ORDER BY name LIMIT ?1 OFFSET ?2",
        -1,
        &statement,
        NULL
    );
    if (prepare_result != SQLITE_OK) {
        return 0;
    }

    sqlite3_bind_int(statement, 1, (int32_t)max_pins + 1);
    sqlite3_bind_int(statement, 2, offset);
    while (sqlite3_step(statement) == SQLITE_ROW) {
        if (count == max_pins) {
            *more = true;
            break;
        }
        snprintf(pins[count].agent_name, sizeof(pins[count].agent_name), "%s", (const char *)sqlite3_column_text(statement, 0));
        snprintf(pins[count].fingerprint_hex, sizeof(pins[count].fingerprint_hex), "%s", (const char *)sqlite3_column_text(statement, 1));
        count++;
    }
    sqlite3_finalize(statement);

    return count;
}

static bool insertPin(IdentityDatabase *self, const char *sql, const char *agent_name, const char *fingerprint_hex)
{
    sqlite3_stmt *statement = NULL;
    bool inserted;

    if (sqlite3_prepare_v2(self->database, sql, -1, &statement, NULL) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(statement, 1, agent_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(statement, 2, fingerprint_hex, -1, SQLITE_STATIC);
    inserted = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);

    return inserted;
}

static bool aclWriteAllowed(void *context, const char *fingerprint_hex, const char *agent_name, const char *interface_name)
{
    IdentityDatabase *self = context;
    sqlite3_stmt *statement = NULL;
    bool allowed = false;

    if (sqlite3_prepare_v2(
            self->database,
            "SELECT can_write FROM client_acls"
            " WHERE agent_name = ?2 AND interface_name = ?3 AND fingerprint IN (?1, 'default')"
            " ORDER BY (fingerprint = 'default') LIMIT 1",
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(statement, 1, fingerprint_hex, -1, SQLITE_STATIC);
    sqlite3_bind_text(statement, 2, agent_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(statement, 3, interface_name, -1, SQLITE_STATIC);
    if (sqlite3_step(statement) == SQLITE_ROW) {
        allowed = sqlite3_column_int(statement, 0) != 0;
    }
    sqlite3_finalize(statement);

    return allowed;
}

static bool aclGrant(
    void *context,
    const char *fingerprint_hex,
    const char *agent_name,
    const char *interface_name,
    bool can_write
)
{
    IdentityDatabase *self = context;
    sqlite3_stmt *statement = NULL;
    bool granted;

    if (sqlite3_prepare_v2(
            self->database,
            "INSERT INTO client_acls (fingerprint, agent_name, interface_name, can_write) VALUES (?1, ?2, ?3, ?4)"
            " ON CONFLICT (fingerprint, agent_name, interface_name) DO UPDATE SET can_write = ?4",
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(statement, 1, fingerprint_hex, -1, SQLITE_STATIC);
    sqlite3_bind_text(statement, 2, agent_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(statement, 3, interface_name, -1, SQLITE_STATIC);
    sqlite3_bind_int(statement, 4, can_write ? 1 : 0);
    granted = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);

    return granted;
}

static bool aclRevoke(void *context, const char *fingerprint_hex, const char *agent_name, const char *interface_name)
{
    IdentityDatabase *self = context;
    sqlite3_stmt *statement = NULL;
    bool revoked = false;

    if (sqlite3_prepare_v2(
            self->database,
            "DELETE FROM client_acls WHERE fingerprint = ?1 AND agent_name = ?2 AND interface_name = ?3",
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(statement, 1, fingerprint_hex, -1, SQLITE_STATIC);
    sqlite3_bind_text(statement, 2, agent_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(statement, 3, interface_name, -1, SQLITE_STATIC);
    if (sqlite3_step(statement) == SQLITE_DONE) {
        revoked = sqlite3_changes(self->database) > 0;
    }
    sqlite3_finalize(statement);

    return revoked;
}

static uint8_t aclList(void *context, uint16_t offset, AclEntry *entries, uint8_t max_entries, bool *more)
{
    IdentityDatabase *self = context;
    sqlite3_stmt *statement = NULL;
    uint8_t count = 0;

    *more = false;
    if (sqlite3_prepare_v2(
            self->database,
            "SELECT fingerprint, agent_name, interface_name, can_write FROM client_acls"
            " ORDER BY agent_name, interface_name, fingerprint LIMIT ?1 OFFSET ?2",
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        return 0;
    }

    sqlite3_bind_int(statement, 1, (int32_t)max_entries + 1);
    sqlite3_bind_int(statement, 2, offset);
    while (sqlite3_step(statement) == SQLITE_ROW) {
        if (count == max_entries) {
            *more = true;
            break;
        }
        snprintf(entries[count].fingerprint_hex, sizeof(entries[count].fingerprint_hex), "%s", (const char *)sqlite3_column_text(statement, 0));
        snprintf(entries[count].agent_name, sizeof(entries[count].agent_name), "%s", (const char *)sqlite3_column_text(statement, 1));
        snprintf(entries[count].interface_name, sizeof(entries[count].interface_name), "%s", (const char *)sqlite3_column_text(statement, 2));
        entries[count].can_write = sqlite3_column_int(statement, 3) != 0;
        count++;
    }
    sqlite3_finalize(statement);

    return count;
}
