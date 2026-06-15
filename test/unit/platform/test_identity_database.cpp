#include <cest>

#include <cstdio>
#include <cstring>

extern "C" {
#include "platform/linux/sqlite/identity_database.h"
}

#define TRUCK_FINGERPRINT "aa11bb22cc33dd44ee55ff66aa77bb88cc99dd00ee11ff22aa33bb44cc55dd66"
#define OTHER_FINGERPRINT "0000000000000000000000000000000000000000000000000000000000000000"
#define PIN_FILE_PATH "/tmp/can_hub_test_known_agents"
#define DB_FILE_PATH "/tmp/can_hub_test_migrations.db"

static IdentityDatabase database;
static IdentityStorePort *port;

static int32_t readUserVersionFile(const char *path);
static void executeRaw(const char *path, const char *sql);

describe("identity_database", []() {
    beforeEach([]() {
        IdentityDatabase_Open(&database, ":memory:");
        port = IdentityDatabase_Port(&database);
    });

    afterEach([]() {
        IdentityDatabase_Close(&database);
        std::remove(PIN_FILE_PATH);
    });

    it("misses a name that was never pinned", []() {
        char fingerprint[IDENTITY_FINGERPRINT_HEX_SIZE];

        expect(port->lookup(port->context, "truck42", fingerprint)).toBe(false);
    });

    it("pins and looks up a fingerprint", []() {
        char fingerprint[IDENTITY_FINGERPRINT_HEX_SIZE];

        expect(port->pin(port->context, "truck42", TRUCK_FINGERPRINT)).toBe(true);
        expect(port->lookup(port->context, "truck42", fingerprint)).toBe(true);
        expect((const char *)fingerprint).toBe(TRUCK_FINGERPRINT);
    });

    it("rejects pinning an already pinned name", []() {
        port->pin(port->context, "truck42", TRUCK_FINGERPRINT);

        expect(port->pin(port->context, "truck42", OTHER_FINGERPRINT)).toBe(false);
    });

    it("forgets a pinned name so it can be pinned again", []() {
        char fingerprint[IDENTITY_FINGERPRINT_HEX_SIZE];

        port->pin(port->context, "truck42", TRUCK_FINGERPRINT);

        expect(port->forget(port->context, "truck42")).toBe(true);
        expect(port->lookup(port->context, "truck42", fingerprint)).toBe(false);
        expect(port->pin(port->context, "truck42", OTHER_FINGERPRINT)).toBe(true);
    });

    it("refuses to forget a name that was never pinned", []() {
        expect(port->forget(port->context, "ghost")).toBe(false);
    });

    it("reads open and denies writes with no acl", []() {
        AuthorizationPort *acl = IdentityDatabase_AuthorizationPort(&database);

        expect(acl->read_allowed(acl->context, TRUCK_FINGERPRINT, "truck42", "can0")).toBe(true);
        expect(acl->write_allowed(acl->context, TRUCK_FINGERPRINT, "truck42", "can0")).toBe(false);
    });

    it("grants a specific write and scopes it to that fingerprint and interface", []() {
        AuthorizationPort *acl = IdentityDatabase_AuthorizationPort(&database);

        expect(acl->grant(acl->context, TRUCK_FINGERPRINT, "truck42", "can0", true, true)).toBe(true);
        expect(acl->write_allowed(acl->context, TRUCK_FINGERPRINT, "truck42", "can0")).toBe(true);
        expect(acl->write_allowed(acl->context, TRUCK_FINGERPRINT, "truck42", "can1")).toBe(false);
        expect(acl->write_allowed(acl->context, OTHER_FINGERPRINT, "truck42", "can0")).toBe(false);
    });

    it("downgrades and revokes a grant", []() {
        AuthorizationPort *acl = IdentityDatabase_AuthorizationPort(&database);

        acl->grant(acl->context, TRUCK_FINGERPRINT, "truck42", "can0", true, true);
        expect(acl->grant(acl->context, TRUCK_FINGERPRINT, "truck42", "can0", true, false)).toBe(true);
        expect(acl->write_allowed(acl->context, TRUCK_FINGERPRINT, "truck42", "can0")).toBe(false);
        expect(acl->revoke(acl->context, TRUCK_FINGERPRINT, "truck42", "can0")).toBe(true);
        expect(acl->revoke(acl->context, TRUCK_FINGERPRINT, "truck42", "can0")).toBe(false);
    });

    it("applies the global wildcard baseline", []() {
        AuthorizationPort *acl = IdentityDatabase_AuthorizationPort(&database);

        acl->grant(acl->context, "*", "*", "*", true, true);
        expect(acl->write_allowed(acl->context, TRUCK_FINGERPRINT, "truck42", "can0")).toBe(true);
        expect(acl->write_allowed(acl->context, OTHER_FINGERPRINT, "van7", "can9")).toBe(true);
    });

    it("locks an interface for everyone with a wildcard-subject deny", []() {
        AuthorizationPort *acl = IdentityDatabase_AuthorizationPort(&database);

        acl->grant(acl->context, "*", "*", "*", true, false);
        acl->grant(acl->context, "*", "truck42", "can0", false, false);
        expect(acl->read_allowed(acl->context, OTHER_FINGERPRINT, "truck42", "can0")).toBe(false);
        expect(acl->read_allowed(acl->context, OTHER_FINGERPRINT, "truck42", "can1")).toBe(true);
    });

    it("lets a named fingerprint override a wildcard-subject deny (subject dominates)", []() {
        AuthorizationPort *acl = IdentityDatabase_AuthorizationPort(&database);

        acl->grant(acl->context, "*", "truck42", "can0", false, false);
        acl->grant(acl->context, TRUCK_FINGERPRINT, "*", "*", true, true);
        expect(acl->write_allowed(acl->context, TRUCK_FINGERPRINT, "truck42", "can0")).toBe(true);
        expect(acl->write_allowed(acl->context, OTHER_FINGERPRINT, "truck42", "can0")).toBe(false);
    });

    it("resolves the worked example (most specific within subject)", []() {
        AuthorizationPort *acl = IdentityDatabase_AuthorizationPort(&database);

        acl->grant(acl->context, "*", "*", "*", true, true);
        acl->grant(acl->context, TRUCK_FINGERPRINT, "agent1", "*", true, false);
        acl->grant(acl->context, TRUCK_FINGERPRINT, "agent1", "can1", false, false);

        expect(acl->read_allowed(acl->context, TRUCK_FINGERPRINT, "agent1", "can1")).toBe(false);
        expect(acl->write_allowed(acl->context, TRUCK_FINGERPRINT, "agent1", "can1")).toBe(false);
        expect(acl->read_allowed(acl->context, TRUCK_FINGERPRINT, "agent1", "can0")).toBe(true);
        expect(acl->write_allowed(acl->context, TRUCK_FINGERPRINT, "agent1", "can0")).toBe(false);
        expect(acl->read_allowed(acl->context, TRUCK_FINGERPRINT, "agent2", "can0")).toBe(true);
        expect(acl->write_allowed(acl->context, TRUCK_FINGERPRINT, "agent2", "can0")).toBe(true);
    });

    it("lists acl entries with pagination", []() {
        AuthorizationPort *acl = IdentityDatabase_AuthorizationPort(&database);
        AclEntry entries[1];
        bool more;

        acl->grant(acl->context, TRUCK_FINGERPRINT, "truck42", "can0", true, true);
        acl->grant(acl->context, OTHER_FINGERPRINT, "truck42", "can1", true, false);

        expect(acl->list(acl->context, 0, entries, 1, &more)).toBe(1);
        expect(more).toBe(true);
        expect((const char *)entries[0].interface_name).toBe("can0");
        expect(entries[0].can_write).toBe(true);
        expect(acl->list(acl->context, 1, entries, 1, &more)).toBe(1);
        expect(more).toBe(false);
        expect((const char *)entries[0].interface_name).toBe("can1");
        expect(entries[0].can_write).toBe(false);
    });

    it("lists pins ordered by name with pagination", []() {
        IdentityPin pins[2];
        bool more;

        port->pin(port->context, "van7", OTHER_FINGERPRINT);
        port->pin(port->context, "truck42", TRUCK_FINGERPRINT);
        port->pin(port->context, "bus1", OTHER_FINGERPRINT);

        expect(port->list(port->context, 0, pins, 2, &more)).toBe(2);
        expect(more).toBe(true);
        expect((const char *)pins[0].agent_name).toBe("bus1");
        expect((const char *)pins[1].agent_name).toBe("truck42");
        expect((const char *)pins[1].fingerprint_hex).toBe(TRUCK_FINGERPRINT);

        expect(port->list(port->context, 2, pins, 2, &more)).toBe(1);
        expect(more).toBe(false);
        expect((const char *)pins[0].agent_name).toBe("van7");
    });

    it("fully terminates a listed pin entry even with an empty name", []() {
        IdentityPin pins[1];
        bool more;

        memset(pins, 0xFF, sizeof(pins));
        port->pin(port->context, "", TRUCK_FINGERPRINT);

        expect(port->list(port->context, 0, pins, 1, &more)).toBe(1);
        expect((int)pins[0].agent_name[0]).toBe(0);
        expect((int)pins[0].agent_name[REGISTER_AGENT_NAME_SIZE - 1]).toBe(0);
    });

    it("imports a known_agents pin file without overwriting existing pins", []() {
        char fingerprint[IDENTITY_FINGERPRINT_HEX_SIZE];
        FILE *pin_file = fopen(PIN_FILE_PATH, "w");

        fprintf(pin_file, "truck42 %s\n", OTHER_FINGERPRINT);
        fprintf(pin_file, "van7 %s\n", TRUCK_FINGERPRINT);
        fclose(pin_file);

        port->pin(port->context, "truck42", TRUCK_FINGERPRINT);

        expect(IdentityDatabase_ImportPinFile(&database, PIN_FILE_PATH)).toBe(true);
        expect(port->lookup(port->context, "truck42", fingerprint)).toBe(true);
        expect((const char *)fingerprint).toBe(TRUCK_FINGERPRINT);
        expect(port->lookup(port->context, "van7", fingerprint)).toBe(true);
    });
});

describe("identity_database migrations", []() {
    afterEach([]() {
        std::remove(DB_FILE_PATH);
    });

    it("brings a fresh db to the latest schema version", []() {
        IdentityDatabase fresh;

        expect(IdentityDatabase_Open(&fresh, DB_FILE_PATH)).toBe(true);
        IdentityDatabase_Close(&fresh);

        expect(readUserVersionFile(DB_FILE_PATH)).toBe(1);
    });

    it("migrates a legacy db that has tables but no recorded version", []() {
        IdentityDatabase migrated;
        char fingerprint[IDENTITY_FINGERPRINT_HEX_SIZE];

        executeRaw(
            DB_FILE_PATH,
            "CREATE TABLE agent_identities (name TEXT PRIMARY KEY, fingerprint TEXT NOT NULL,"
            " first_seen_at INTEGER NOT NULL DEFAULT 0);"
            "INSERT INTO agent_identities (name, fingerprint) VALUES ('truck42', '" TRUCK_FINGERPRINT "');"
            "PRAGMA user_version = 0"
        );

        expect(IdentityDatabase_Open(&migrated, DB_FILE_PATH)).toBe(true);
        IdentityStorePort *migrated_port = IdentityDatabase_Port(&migrated);
        expect(migrated_port->lookup(migrated_port->context, "truck42", fingerprint)).toBe(true);
        expect((const char *)fingerprint).toBe(TRUCK_FINGERPRINT);
        IdentityDatabase_Close(&migrated);

        expect(readUserVersionFile(DB_FILE_PATH)).toBe(1);
    });

    it("refuses to open a db newer than the binary supports", []() {
        IdentityDatabase newer;

        executeRaw(DB_FILE_PATH, "PRAGMA user_version = 999");

        expect(IdentityDatabase_Open(&newer, DB_FILE_PATH)).toBe(false);
    });
});

static int32_t readUserVersionFile(const char *path)
{
    sqlite3 *database = nullptr;
    sqlite3_stmt *statement = nullptr;
    int32_t version = -1;

    sqlite3_open(path, &database);
    sqlite3_prepare_v2(database, "PRAGMA user_version", -1, &statement, nullptr);
    if (sqlite3_step(statement) == SQLITE_ROW) {
        version = sqlite3_column_int(statement, 0);
    }
    sqlite3_finalize(statement);
    sqlite3_close(database);

    return version;
}

static void executeRaw(const char *path, const char *sql)
{
    sqlite3 *database = nullptr;

    sqlite3_open(path, &database);
    sqlite3_exec(database, sql, nullptr, nullptr, nullptr);
    sqlite3_close(database);
}
