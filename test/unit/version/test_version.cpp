#include <cest>

#include <cstring>

extern "C" {
#include "version.h"
}

describe("version", []() {
    it("reports the project version", []() {
        expect(strcmp(Version_String(), CAN_HUB_VERSION) == 0).toBe(true);
    });
});
