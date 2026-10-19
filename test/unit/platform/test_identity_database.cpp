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
#define LATEST_SCHEMA_VERSION 4

static IdentityDatabase database;
static IdentityStorePort *port;
static BridgeStorePort *bridges;

static int32_t readUserVersionFile(const char *path);
static BridgeRoute route(const char *source_agent, const char *destination_agent);
static BridgeGroup group(const char *name);
static void executeRaw(const char *path, const char *sql);
static bool grant(AuthorizationPort *acl, const char *fingerprint_hex, const char *agent_name, const char *interface_name, const char *level);

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
        bool pinned;
        bool found;

        pinned = port->pin(port->context, "truck42", TRUCK_FINGERPRINT);
        found = port->lookup(port->context, "truck42", fingerprint);

        expect(pinned).toBe(true);
        expect(found).toBe(true);
        expect((const char *)fingerprint).toBe(TRUCK_FINGERPRINT);
    });

    it("rejects pinning an already pinned name", []() {
        port->pin(port->context, "truck42", TRUCK_FINGERPRINT);

        expect(port->pin(port->context, "truck42", OTHER_FINGERPRINT)).toBe(false);
    });

    it("forgets a pinned name so it can be pinned again", []() {
        char fingerprint[IDENTITY_FINGERPRINT_HEX_SIZE];
        bool forgotten;
        bool found;
        bool pinned_again;

        port->pin(port->context, "truck42", TRUCK_FINGERPRINT);

        forgotten = port->forget(port->context, "truck42");
        found = port->lookup(port->context, "truck42", fingerprint);
        pinned_again = port->pin(port->context, "truck42", OTHER_FINGERPRINT);

        expect(forgotten).toBe(true);
        expect(found).toBe(false);
        expect(pinned_again).toBe(true);
    });

    it("refuses to forget a name that was never pinned", []() {
        expect(port->forget(port->context, "ghost")).toBe(false);
    });

    it("reads open and denies writes with no acl", []() {
        AuthorizationPort *acl = IdentityDatabase_AuthorizationPort(&database);

        expect(acl->read_allowed(acl->context, TRUCK_FINGERPRINT, "truck42", "can0")).toBe(true);
        expect(acl->write_allowed(acl->context, TRUCK_FINGERPRINT, "truck42", "can0")).toBe(false);
    });

    it("knows a client once a rule names its fingerprint, even a deny", []() {
        AuthorizationPort *acl = IdentityDatabase_AuthorizationPort(&database);

        grant(acl, "*", "*", "*", "rw");
        grant(acl, TRUCK_FINGERPRINT, "truck42", "can0", "none");

        expect(acl->knows(acl->context, TRUCK_FINGERPRINT)).toBe(true);
        expect(acl->knows(acl->context, OTHER_FINGERPRINT)).toBe(false);
    });

    it("grants a specific write and scopes it to that fingerprint and interface", []() {
        AuthorizationPort *acl = IdentityDatabase_AuthorizationPort(&database);
        bool granted;

        granted = grant(acl, TRUCK_FINGERPRINT, "truck42", "can0", "rw");

        expect(granted).toBe(true);
        expect(acl->write_allowed(acl->context, TRUCK_FINGERPRINT, "truck42", "can0")).toBe(true);
        expect(acl->write_allowed(acl->context, TRUCK_FINGERPRINT, "truck42", "can1")).toBe(false);
        expect(acl->write_allowed(acl->context, OTHER_FINGERPRINT, "truck42", "can0")).toBe(false);
    });

    it("downgrades a grant to read only", []() {
        AuthorizationPort *acl = IdentityDatabase_AuthorizationPort(&database);
        bool downgraded;

        grant(acl, TRUCK_FINGERPRINT, "truck42", "can0", "rw");

        downgraded = grant(acl, TRUCK_FINGERPRINT, "truck42", "can0", "r");

        expect(downgraded).toBe(true);
        expect(acl->write_allowed(acl->context, TRUCK_FINGERPRINT, "truck42", "can0")).toBe(false);
    });

    it("revokes a grant once", []() {
        AuthorizationPort *acl = IdentityDatabase_AuthorizationPort(&database);
        bool first_revoked;
        bool second_revoked;

        grant(acl, TRUCK_FINGERPRINT, "truck42", "can0", "rw");

        first_revoked = acl->revoke(acl->context, TRUCK_FINGERPRINT, "truck42", "can0");
        second_revoked = acl->revoke(acl->context, TRUCK_FINGERPRINT, "truck42", "can0");

        expect(first_revoked).toBe(true);
        expect(second_revoked).toBe(false);
    });

    it("applies the global wildcard baseline", []() {
        AuthorizationPort *acl = IdentityDatabase_AuthorizationPort(&database);

        grant(acl, "*", "*", "*", "rw");

        expect(acl->write_allowed(acl->context, TRUCK_FINGERPRINT, "truck42", "can0")).toBe(true);
        expect(acl->write_allowed(acl->context, OTHER_FINGERPRINT, "van7", "can9")).toBe(true);
    });

    it("locks an interface for everyone with a wildcard-subject deny", []() {
        AuthorizationPort *acl = IdentityDatabase_AuthorizationPort(&database);

        grant(acl, "*", "*", "*", "r");
        grant(acl, "*", "truck42", "can0", "none");

        expect(acl->read_allowed(acl->context, OTHER_FINGERPRINT, "truck42", "can0")).toBe(false);
        expect(acl->read_allowed(acl->context, OTHER_FINGERPRINT, "truck42", "can1")).toBe(true);
    });

    it("lets a named fingerprint override a wildcard-subject deny (subject dominates)", []() {
        AuthorizationPort *acl = IdentityDatabase_AuthorizationPort(&database);

        grant(acl, "*", "truck42", "can0", "none");
        grant(acl, TRUCK_FINGERPRINT, "*", "*", "rw");

        expect(acl->write_allowed(acl->context, TRUCK_FINGERPRINT, "truck42", "can0")).toBe(true);
        expect(acl->write_allowed(acl->context, OTHER_FINGERPRINT, "truck42", "can0")).toBe(false);
    });

    it("resolves the worked example (most specific within subject)", []() {
        AuthorizationPort *acl = IdentityDatabase_AuthorizationPort(&database);

        grant(acl, "*", "*", "*", "rw");
        grant(acl, TRUCK_FINGERPRINT, "agent1", "*", "r");
        grant(acl, TRUCK_FINGERPRINT, "agent1", "can1", "none");

        expect(acl->read_allowed(acl->context, TRUCK_FINGERPRINT, "agent1", "can1")).toBe(false);
        expect(acl->write_allowed(acl->context, TRUCK_FINGERPRINT, "agent1", "can1")).toBe(false);
        expect(acl->read_allowed(acl->context, TRUCK_FINGERPRINT, "agent1", "can0")).toBe(true);
        expect(acl->write_allowed(acl->context, TRUCK_FINGERPRINT, "agent1", "can0")).toBe(false);
        expect(acl->read_allowed(acl->context, TRUCK_FINGERPRINT, "agent2", "can0")).toBe(true);
        expect(acl->write_allowed(acl->context, TRUCK_FINGERPRINT, "agent2", "can0")).toBe(true);
    });

    it("grants admin only through the rwa level", []() {
        AuthorizationPort *acl = IdentityDatabase_AuthorizationPort(&database);

        grant(acl, TRUCK_FINGERPRINT, "truck42", "can0", "rwa");
        grant(acl, TRUCK_FINGERPRINT, "truck42", "can1", "rw");

        expect(acl->admin_allowed(acl->context, TRUCK_FINGERPRINT, "truck42", "can0")).toBe(true);
        expect(acl->write_allowed(acl->context, TRUCK_FINGERPRINT, "truck42", "can0")).toBe(true);
        expect(acl->admin_allowed(acl->context, TRUCK_FINGERPRINT, "truck42", "can1")).toBe(false);
        expect(acl->admin_allowed(acl->context, OTHER_FINGERPRINT, "truck42", "can0")).toBe(false);
    });

    it("lists the first page of acl entries and flags more", []() {
        AuthorizationPort *acl = IdentityDatabase_AuthorizationPort(&database);
        AclEntry entries[1];
        uint8_t count;
        bool more;

        grant(acl, TRUCK_FINGERPRINT, "truck42", "can0", "rw");
        grant(acl, OTHER_FINGERPRINT, "truck42", "can1", "r");

        count = acl->list(acl->context, 0, entries, 1, &more);

        expect(count).toBe(1);
        expect(more).toBe(true);
        expect((const char *)entries[0].interface_name).toBe("can0");
        expect(entries[0].can_write).toBe(true);
    });

    it("lists the last page of acl entries from an offset", []() {
        AuthorizationPort *acl = IdentityDatabase_AuthorizationPort(&database);
        AclEntry entries[1];
        uint8_t count;
        bool more;

        grant(acl, TRUCK_FINGERPRINT, "truck42", "can0", "rw");
        grant(acl, OTHER_FINGERPRINT, "truck42", "can1", "r");

        count = acl->list(acl->context, 1, entries, 1, &more);

        expect(count).toBe(1);
        expect(more).toBe(false);
        expect((const char *)entries[0].interface_name).toBe("can1");
        expect(entries[0].can_write).toBe(false);
    });

    it("lists the first page of pins ordered by name", []() {
        IdentityPin pins[2];
        uint8_t count;
        bool more;

        port->pin(port->context, "van7", OTHER_FINGERPRINT);
        port->pin(port->context, "truck42", TRUCK_FINGERPRINT);
        port->pin(port->context, "bus1", OTHER_FINGERPRINT);

        count = port->list(port->context, 0, pins, 2, &more);

        expect(count).toBe(2);
        expect(more).toBe(true);
        expect((const char *)pins[0].agent_name).toBe("bus1");
        expect((const char *)pins[1].agent_name).toBe("truck42");
        expect((const char *)pins[1].fingerprint_hex).toBe(TRUCK_FINGERPRINT);
    });

    it("lists the last page of pins from an offset", []() {
        IdentityPin pins[2];
        uint8_t count;
        bool more;

        port->pin(port->context, "van7", OTHER_FINGERPRINT);
        port->pin(port->context, "truck42", TRUCK_FINGERPRINT);
        port->pin(port->context, "bus1", OTHER_FINGERPRINT);

        count = port->list(port->context, 2, pins, 2, &more);

        expect(count).toBe(1);
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

describe("identity_database bridge routes", []() {
    beforeEach([]() {
        IdentityDatabase_Open(&database, ":memory:");
        bridges = IdentityDatabase_BridgePort(&database);
    });

    afterEach([]() {
        IdentityDatabase_Close(&database);
    });

    it("loads a saved route with its filters", []() {
        BridgeRoute saved = route("truck42", "bus7");
        BridgeRoute loaded;
        bool found;

        saved.filter_count = 1;
        saved.filters[0] = { 0x100, 0x7FF };

        bridges->save(bridges->context, &saved);
        found = bridges->load(bridges->context, 0, &loaded);

        expect(found).toBe(true);
        expect((const char *)loaded.source.agent_name).toBe("truck42");
        expect((const char *)loaded.destination.agent_name).toBe("bus7");
        expect(loaded.filter_count).toBe(1);
        expect(loaded.filters[0].can_id).toBe((uint32_t)0x100);
        expect(loaded.filters[0].can_mask).toBe((uint32_t)0x7FF);
    });

    it("replaces the filters of a route saved again", []() {
        BridgeRoute saved = route("truck42", "bus7");
        BridgeRoute loaded;
        BridgeRoute extra;
        bool second_found;

        bridges->save(bridges->context, &saved);
        saved.filter_count = 1;
        saved.filters[0] = { 0x200, 0x7FF };

        bridges->save(bridges->context, &saved);
        bridges->load(bridges->context, 0, &loaded);
        second_found = bridges->load(bridges->context, 1, &extra);

        expect(second_found).toBe(false);
        expect(loaded.filters[0].can_id).toBe((uint32_t)0x200);
    });

    it("forgets a removed route", []() {
        BridgeRoute saved = route("truck42", "bus7");
        BridgeRoute loaded;

        bridges->save(bridges->context, &saved);

        bridges->remove(bridges->context, &saved.source, &saved.destination);

        expect(bridges->load(bridges->context, 0, &loaded)).toBe(false);
    });
});

describe("identity_database bridge groups", []() {
    beforeEach([]() {
        IdentityDatabase_Open(&database, ":memory:");
        bridges = IdentityDatabase_BridgePort(&database);
    });

    afterEach([]() {
        IdentityDatabase_Close(&database);
    });

    it("loads a saved group with its filters and members", []() {
        BridgeGroup saved = group("fleet");
        BridgeEndpoint truck = route("truck42", "bus7").source;
        BridgeGroup loaded;
        bool found;

        saved.filter_count = 1;
        saved.filters[0] = { 0x100, 0x7FF };
        bridges->save_group(bridges->context, &saved);

        bridges->add_member(bridges->context, "fleet", &truck);
        found = bridges->load_group(bridges->context, 0, &loaded);

        expect(found).toBe(true);
        expect((const char *)loaded.name).toBe("fleet");
        expect(loaded.filters[0].can_id).toBe((uint32_t)0x100);
        expect(loaded.member_count).toBe(1);
        expect((const char *)loaded.members[0].agent_name).toBe("truck42");
    });

    it("forgets a member that leaves", []() {
        BridgeGroup saved = group("fleet");
        BridgeEndpoint truck = route("truck42", "bus7").source;
        BridgeGroup loaded;

        bridges->save_group(bridges->context, &saved);
        bridges->add_member(bridges->context, "fleet", &truck);

        bridges->remove_member(bridges->context, "fleet", &truck);
        bridges->load_group(bridges->context, 0, &loaded);

        expect(loaded.member_count).toBe(0);
    });

    it("forgets a removed group with its members", []() {
        BridgeGroup saved = group("fleet");
        BridgeEndpoint truck = route("truck42", "bus7").source;
        BridgeGroup recreated = group("fleet");
        BridgeGroup loaded;

        bridges->save_group(bridges->context, &saved);
        bridges->add_member(bridges->context, "fleet", &truck);

        bridges->remove_group(bridges->context, "fleet");
        bridges->save_group(bridges->context, &recreated);
        bridges->load_group(bridges->context, 0, &loaded);

        expect(bridges->load_group(bridges->context, 1, &loaded)).toBe(false);
        expect(loaded.member_count).toBe(0);
    });
});

describe("identity_database migrations", []() {
    afterEach([]() {
        std::remove(DB_FILE_PATH);
    });

    it("brings a fresh db to the latest schema version", []() {
        IdentityDatabase fresh;
        bool opened;

        opened = IdentityDatabase_Open(&fresh, DB_FILE_PATH);
        IdentityDatabase_Close(&fresh);

        expect(opened).toBe(true);
        expect(readUserVersionFile(DB_FILE_PATH)).toBe(LATEST_SCHEMA_VERSION);
    });

    it("migrates a legacy db that has tables but no recorded version", []() {
        IdentityDatabase migrated;
        IdentityStorePort *migrated_port;
        char fingerprint[IDENTITY_FINGERPRINT_HEX_SIZE];
        bool opened;
        bool found;

        executeRaw(
            DB_FILE_PATH,
            "CREATE TABLE agent_identities (name TEXT PRIMARY KEY, fingerprint TEXT NOT NULL,"
            " first_seen_at INTEGER NOT NULL DEFAULT 0);"
            "INSERT INTO agent_identities (name, fingerprint) VALUES ('truck42', '" TRUCK_FINGERPRINT "');"
            "PRAGMA user_version = 0"
        );

        opened = IdentityDatabase_Open(&migrated, DB_FILE_PATH);
        migrated_port = IdentityDatabase_Port(&migrated);
        found = migrated_port->lookup(migrated_port->context, "truck42", fingerprint);
        IdentityDatabase_Close(&migrated);

        expect(opened).toBe(true);
        expect(found).toBe(true);
        expect((const char *)fingerprint).toBe(TRUCK_FINGERPRINT);
        expect(readUserVersionFile(DB_FILE_PATH)).toBe(LATEST_SCHEMA_VERSION);
    });

    it("keeps the grants of a version 1 db and gives them no admin", []() {
        IdentityDatabase migrated;
        AuthorizationPort *acl;
        bool opened;
        bool may_write;
        bool may_configure;

        executeRaw(
            DB_FILE_PATH,
            "CREATE TABLE client_acls (fingerprint TEXT NOT NULL, agent_name TEXT NOT NULL,"
            " interface_name TEXT NOT NULL, can_read INTEGER NOT NULL DEFAULT 1,"
            " can_write INTEGER NOT NULL DEFAULT 0, PRIMARY KEY (fingerprint, agent_name, interface_name));"
            "INSERT INTO client_acls VALUES ('" TRUCK_FINGERPRINT "', 'truck42', 'can0', 1, 1);"
            "PRAGMA user_version = 1"
        );

        opened = IdentityDatabase_Open(&migrated, DB_FILE_PATH);
        acl = IdentityDatabase_AuthorizationPort(&migrated);
        may_write = acl->write_allowed(acl->context, TRUCK_FINGERPRINT, "truck42", "can0");
        may_configure = acl->admin_allowed(acl->context, TRUCK_FINGERPRINT, "truck42", "can0");
        IdentityDatabase_Close(&migrated);

        expect(opened).toBe(true);
        expect(may_write).toBe(true);
        expect(may_configure).toBe(false);
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

static bool grant(AuthorizationPort *acl, const char *fingerprint_hex, const char *agent_name, const char *interface_name, const char *level)
{
    AclEntry entry;

    memset(&entry, 0, sizeof(entry));
    snprintf(entry.fingerprint_hex, sizeof(entry.fingerprint_hex), "%s", fingerprint_hex);
    snprintf(entry.agent_name, sizeof(entry.agent_name), "%s", agent_name);
    snprintf(entry.interface_name, sizeof(entry.interface_name), "%s", interface_name);
    entry.can_read = strchr(level, 'r') != NULL;
    entry.can_write = strchr(level, 'w') != NULL;
    entry.can_admin = strchr(level, 'a') != NULL;

    return acl->grant(acl->context, &entry);
}

static BridgeRoute route(const char *source_agent, const char *destination_agent)
{
    BridgeRoute built;

    memset(&built, 0, sizeof(built));
    snprintf(built.source.agent_name, sizeof(built.source.agent_name), "%s", source_agent);
    snprintf(built.source.interface_name, sizeof(built.source.interface_name), "can0");
    snprintf(built.destination.agent_name, sizeof(built.destination.agent_name), "%s", destination_agent);
    snprintf(built.destination.interface_name, sizeof(built.destination.interface_name), "can0");

    return built;
}

static BridgeGroup group(const char *name)
{
    BridgeGroup built;

    memset(&built, 0, sizeof(built));
    snprintf(built.name, sizeof(built.name), "%s", name);

    return built;
}
