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
