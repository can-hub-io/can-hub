#include <cest>

#include <cstring>

extern "C" {
#include "version.h"
}

describe("version", []() {
    it("reports a non-empty dotted version", []() {
        const char *version = Version_String();

        expect(version != nullptr).toBe(true);
        expect(strlen(version) > 0).toBe(true);
        expect(strchr(version, '.') != nullptr).toBe(true);
    });
});
