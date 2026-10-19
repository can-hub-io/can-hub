#include "platform/linux/sqlite/identity_database.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LINE_BUFFER_SIZE 512
#define IMPORT_KEY_MAX 256
#define PRAGMA_BUFFER_SIZE 64
#define SCHEMA_VERSION 2

#define SCHEMA_BASELINE \
    "CREATE TABLE IF NOT EXISTS agent_identities (" \
    "name TEXT PRIMARY KEY," \
    "fingerprint TEXT NOT NULL," \
    "first_seen_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))" \
    ");" \
    "CREATE TABLE IF NOT EXISTS client_acls (" \
    "fingerprint TEXT NOT NULL," \
    "agent_name TEXT NOT NULL," \
    "interface_name TEXT NOT NULL," \
    "can_read INTEGER NOT NULL DEFAULT 1," \
    "can_write INTEGER NOT NULL DEFAULT 0," \
    "PRIMARY KEY (fingerprint, agent_name, interface_name)" \
    ")"

#define SCHEMA_ACL_ADMIN "ALTER TABLE client_acls ADD COLUMN can_admin INTEGER NOT NULL DEFAULT 0"

typedef struct {
    int32_t version;
    const char *statements;
} SchemaMigration;

static const SchemaMigration SCHEMA_MIGRATIONS[] = {
    { 1, SCHEMA_BASELINE },
    { 2, SCHEMA_ACL_ADMIN },
};

static bool migrateSchema(IdentityDatabase *self);
static bool readUserVersion(sqlite3 *database, int32_t *version);
static bool setUserVersion(sqlite3 *database, int32_t version);
static bool storeLookup(void *context, const char *agent_name, char *fingerprint_hex);
static bool storePin(void *context, const char *agent_name, const char *fingerprint_hex);
static bool storeForget(void *context, const char *agent_name);
static uint8_t storeList(void *context, uint16_t offset, IdentityPin *pins, uint8_t max_pins, bool *more);
static bool insertPin(IdentityDatabase *self, const char *sql, const char *agent_name, const char *fingerprint_hex);
static bool aclReadAllowed(void *context, const char *fingerprint_hex, const char *agent_name, const char *interface_name);
static bool aclWriteAllowed(void *context, const char *fingerprint_hex, const char *agent_name, const char *interface_name);
static bool aclAdminAllowed(void *context, const char *fingerprint_hex, const char *agent_name, const char *interface_name);
static void aclResolve(
    IdentityDatabase *self,
    const char *fingerprint_hex,
    const char *agent_name,
    const char *interface_name,
    AclEntry *entry
);
static bool aclGrant(void *context, const AclEntry *entry);
static bool aclRevoke(void *context, const char *fingerprint_hex, const char *agent_name, const char *interface_name);
static uint8_t aclList(void *context, uint16_t offset, AclEntry *entries, uint8_t max_entries, bool *more);
static bool aclKnows(void *context, const char *fingerprint_hex);

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
    self->authorization_port.read_allowed = aclReadAllowed;
    self->authorization_port.write_allowed = aclWriteAllowed;
    self->authorization_port.admin_allowed = aclAdminAllowed;
    self->authorization_port.grant = aclGrant;
    self->authorization_port.revoke = aclRevoke;
    self->authorization_port.list = aclList;
    self->authorization_port.knows = aclKnows;

    if (sqlite3_open(path, &self->database) != SQLITE_OK) {
        IdentityDatabase_Close(self);
        return false;
    }
    if (!migrateSchema(self)) {
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

static bool migrateSchema(IdentityDatabase *self)
{
    int32_t version = 0;
    size_t step;

    if (!readUserVersion(self->database, &version)) {
        return false;
    }
    if (version > SCHEMA_VERSION) {
        fprintf(
            stderr,
            "hub.db schema version %d is newer than this binary supports (%d); refusing to start\n",
            version,
            SCHEMA_VERSION
        );
        return false;
    }
    if (version == SCHEMA_VERSION) {
        return true;
    }

    if (sqlite3_exec(self->database, "BEGIN", NULL, NULL, NULL) != SQLITE_OK) {
        return false;
    }
    for (step = 0; step < sizeof(SCHEMA_MIGRATIONS) / sizeof(SCHEMA_MIGRATIONS[0]); step++) {
        if (SCHEMA_MIGRATIONS[step].version <= version) {
            continue;
        }
        if (sqlite3_exec(self->database, SCHEMA_MIGRATIONS[step].statements, NULL, NULL, NULL) != SQLITE_OK) {
            sqlite3_exec(self->database, "ROLLBACK", NULL, NULL, NULL);
            return false;
        }
    }
    if (!setUserVersion(self->database, SCHEMA_VERSION)) {
        sqlite3_exec(self->database, "ROLLBACK", NULL, NULL, NULL);
        return false;
    }

    return sqlite3_exec(self->database, "COMMIT", NULL, NULL, NULL) == SQLITE_OK;
}

static bool readUserVersion(sqlite3 *database, int32_t *version)
{
    sqlite3_stmt *statement = NULL;
    bool read = false;

    if (sqlite3_prepare_v2(database, "PRAGMA user_version", -1, &statement, NULL) != SQLITE_OK) {
        return false;
    }
    if (sqlite3_step(statement) == SQLITE_ROW) {
        *version = sqlite3_column_int(statement, 0);
        read = true;
    }
    sqlite3_finalize(statement);

    return read;
}

static bool setUserVersion(sqlite3 *database, int32_t version)
{
    char pragma[PRAGMA_BUFFER_SIZE];

    snprintf(pragma, sizeof(pragma), "PRAGMA user_version = %d", version);

    return sqlite3_exec(database, pragma, NULL, NULL, NULL) == SQLITE_OK;
}

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
        memset(&pins[count], 0, sizeof(pins[count]));
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

static bool aclReadAllowed(void *context, const char *fingerprint_hex, const char *agent_name, const char *interface_name)
{
    AclEntry entry;

    aclResolve(context, fingerprint_hex, agent_name, interface_name, &entry);

    return entry.can_read;
}

static bool aclWriteAllowed(void *context, const char *fingerprint_hex, const char *agent_name, const char *interface_name)
{
    AclEntry entry;

    aclResolve(context, fingerprint_hex, agent_name, interface_name, &entry);

    return entry.can_write;
}

static bool aclAdminAllowed(void *context, const char *fingerprint_hex, const char *agent_name, const char *interface_name)
{
    AclEntry entry;

    aclResolve(context, fingerprint_hex, agent_name, interface_name, &entry);

    return entry.can_admin;
}

static void aclResolve(
    IdentityDatabase *self,
    const char *fingerprint_hex,
    const char *agent_name,
    const char *interface_name,
    AclEntry *entry
)
{
    sqlite3_stmt *statement = NULL;

    entry->can_read = true;
    entry->can_write = false;
    entry->can_admin = false;
    if (sqlite3_prepare_v2(
            self->database,
            "SELECT can_read, can_write, can_admin FROM client_acls"
            " WHERE fingerprint IN (?1, '*') AND agent_name IN (?2, '*') AND interface_name IN (?3, '*')"
            " ORDER BY (fingerprint <> '*') DESC,"
            " ((agent_name <> '*') * 2 + (interface_name <> '*')) DESC LIMIT 1",
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        return;
    }

    sqlite3_bind_text(statement, 1, fingerprint_hex, -1, SQLITE_STATIC);
    sqlite3_bind_text(statement, 2, agent_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(statement, 3, interface_name, -1, SQLITE_STATIC);
    if (sqlite3_step(statement) == SQLITE_ROW) {
        entry->can_read = sqlite3_column_int(statement, 0) != 0;
        entry->can_write = sqlite3_column_int(statement, 1) != 0;
        entry->can_admin = sqlite3_column_int(statement, 2) != 0;
    }
    sqlite3_finalize(statement);
}

static bool aclGrant(void *context, const AclEntry *entry)
{
    IdentityDatabase *self = context;
    sqlite3_stmt *statement = NULL;
    bool granted;

    if (sqlite3_prepare_v2(
            self->database,
            "INSERT INTO client_acls (fingerprint, agent_name, interface_name, can_read, can_write, can_admin)"
            " VALUES (?1, ?2, ?3, ?4, ?5, ?6)"
            " ON CONFLICT (fingerprint, agent_name, interface_name)"
            " DO UPDATE SET can_read = ?4, can_write = ?5, can_admin = ?6",
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(statement, 1, entry->fingerprint_hex, -1, SQLITE_STATIC);
    sqlite3_bind_text(statement, 2, entry->agent_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(statement, 3, entry->interface_name, -1, SQLITE_STATIC);
    sqlite3_bind_int(statement, 4, entry->can_read ? 1 : 0);
    sqlite3_bind_int(statement, 5, entry->can_write ? 1 : 0);
    sqlite3_bind_int(statement, 6, entry->can_admin ? 1 : 0);
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
            "SELECT fingerprint, agent_name, interface_name, can_read, can_write, can_admin FROM client_acls"
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
        memset(&entries[count], 0, sizeof(entries[count]));
        snprintf(entries[count].fingerprint_hex, sizeof(entries[count].fingerprint_hex), "%s", (const char *)sqlite3_column_text(statement, 0));
        snprintf(entries[count].agent_name, sizeof(entries[count].agent_name), "%s", (const char *)sqlite3_column_text(statement, 1));
        snprintf(entries[count].interface_name, sizeof(entries[count].interface_name), "%s", (const char *)sqlite3_column_text(statement, 2));
        entries[count].can_read = sqlite3_column_int(statement, 3) != 0;
        entries[count].can_write = sqlite3_column_int(statement, 4) != 0;
        entries[count].can_admin = sqlite3_column_int(statement, 5) != 0;
        count++;
    }
    sqlite3_finalize(statement);

    return count;
}

static bool aclKnows(void *context, const char *fingerprint_hex)
{
    IdentityDatabase *self = context;
    sqlite3_stmt *statement = NULL;
    bool known;

    if (sqlite3_prepare_v2(
            self->database,
            "SELECT 1 FROM client_acls WHERE fingerprint = ?1 LIMIT 1",
            -1,
            &statement,
            NULL
        ) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(statement, 1, fingerprint_hex, -1, SQLITE_STATIC);
    known = sqlite3_step(statement) == SQLITE_ROW;
    sqlite3_finalize(statement);

    return known;
}
