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
    ")"

static bool storeLookup(void *context, const char *agent_name, char *fingerprint_hex);
static bool storePin(void *context, const char *agent_name, const char *fingerprint_hex);
static bool insertPin(IdentityDatabase *self, const char *sql, const char *agent_name, const char *fingerprint_hex);

/* ---------- public ---------- */

bool IdentityDatabase_Open(IdentityDatabase *self, const char *path)
{
    memset(self, 0, sizeof(*self));
    self->port.context = self;
    self->port.lookup = storeLookup;
    self->port.pin = storePin;

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
    return &self->port;
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
