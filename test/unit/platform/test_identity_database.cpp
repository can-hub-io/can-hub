#include <cest>

#include <cstdio>

extern "C" {
#include "platform/linux/sqlite/identity_database.h"
}

#define TRUCK_FINGERPRINT "aa11bb22cc33dd44ee55ff66aa77bb88cc99dd00ee11ff22aa33bb44cc55dd66"
#define OTHER_FINGERPRINT "0000000000000000000000000000000000000000000000000000000000000000"
#define PIN_FILE_PATH "/tmp/can_hub_test_known_agents"

static IdentityDatabase database;
static IdentityStorePort *port;

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

    it("denies writes with no acl and grants them after acl_grant", []() {
        AuthorizationPort *acl = IdentityDatabase_AuthorizationPort(&database);

        expect(acl->write_allowed(acl->context, TRUCK_FINGERPRINT, "truck42", "can0")).toBe(false);
        expect(acl->grant(acl->context, TRUCK_FINGERPRINT, "truck42", "can0", true)).toBe(true);
        expect(acl->write_allowed(acl->context, TRUCK_FINGERPRINT, "truck42", "can0")).toBe(true);
        expect(acl->write_allowed(acl->context, TRUCK_FINGERPRINT, "truck42", "can1")).toBe(false);
        expect(acl->write_allowed(acl->context, OTHER_FINGERPRINT, "truck42", "can0")).toBe(false);
    });

    it("downgrades and revokes a write grant", []() {
        AuthorizationPort *acl = IdentityDatabase_AuthorizationPort(&database);

        acl->grant(acl->context, TRUCK_FINGERPRINT, "truck42", "can0", true);
        expect(acl->grant(acl->context, TRUCK_FINGERPRINT, "truck42", "can0", false)).toBe(true);
        expect(acl->write_allowed(acl->context, TRUCK_FINGERPRINT, "truck42", "can0")).toBe(false);
        expect(acl->revoke(acl->context, TRUCK_FINGERPRINT, "truck42", "can0")).toBe(true);
        expect(acl->revoke(acl->context, TRUCK_FINGERPRINT, "truck42", "can0")).toBe(false);
    });

    it("lists acl entries with pagination", []() {
        AuthorizationPort *acl = IdentityDatabase_AuthorizationPort(&database);
        AclEntry entries[1];
        bool more;

        acl->grant(acl->context, TRUCK_FINGERPRINT, "truck42", "can0", true);
        acl->grant(acl->context, OTHER_FINGERPRINT, "truck42", "can1", false);

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
