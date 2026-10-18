#include <cest>

#include <cstring>

extern "C" {
#include "shared/bounded_string.h"
}

#define DESTINATION_SIZE 8

static char destination[DESTINATION_SIZE];

describe("bounded_string", []() {
    beforeEach([]() {
        memset(destination, 'x', sizeof(destination));
    });

    it("copies a string that fits", []() {
        BoundedString_Copy(destination, sizeof(destination), "can0");

        expect((const char *)destination).toBe("can0");
    });

    it("truncates a string that does not fit and still terminates it", []() {
        BoundedString_Copy(destination, sizeof(destination), "truck42/can0");

        expect((const char *)destination).toBe("truck42");
    });

    it("leaves the destination empty for a missing source", []() {
        BoundedString_Copy(destination, sizeof(destination), NULL);

        expect((const char *)destination).toBe("");
    });
});
