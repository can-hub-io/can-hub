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
