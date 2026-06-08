#include <cest>

#include <cstring>

extern "C" {
#include "socketcand/domain/ascii_framer.h"
}

static AsciiFramer framer;

static bool pushText(const char *text)
{
    return AsciiFramer_Push(&framer, (const uint8_t *)text, strlen(text));
}

describe("ascii_framer", []() {
    beforeEach([]() {
        AsciiFramer_Reset(&framer);
    });

    it("extracts back-to-back commands", []() {
        char command[64];
        size_t length = 0;
        bool first;
        bool second;
        bool third;

        pushText("< open can0 >< rawmode >");
        first = AsciiFramer_Next(&framer, command, sizeof(command), &length);
        bool first_match = strstr(command, "open can0") != nullptr;
        second = AsciiFramer_Next(&framer, command, sizeof(command), &length);
        bool second_match = strstr(command, "rawmode") != nullptr;
        third = AsciiFramer_Next(&framer, command, sizeof(command), &length);

        expect(first).toBe(true);
        expect(first_match).toBe(true);
        expect(second).toBe(true);
        expect(second_match).toBe(true);
        expect(third).toBe(false);
    });

    it("waits for the closing bracket across pushes", []() {
        char command[64];
        size_t length = 0;
        bool incomplete;
        bool complete;

        pushText("< raw");
        incomplete = AsciiFramer_Next(&framer, command, sizeof(command), &length);
        pushText("mode >");
        complete = AsciiFramer_Next(&framer, command, sizeof(command), &length);
        bool match = strstr(command, "rawmode") != nullptr;

        expect(incomplete).toBe(false);
        expect(complete).toBe(true);
        expect(match).toBe(true);
    });

    it("discards junk before a command", []() {
        char command[64];
        size_t length = 0;
        bool found;

        pushText("garbage< echo >");
        found = AsciiFramer_Next(&framer, command, sizeof(command), &length);
        bool match = strstr(command, "echo") != nullptr;

        expect(found).toBe(true);
        expect(match).toBe(true);
    });
});
