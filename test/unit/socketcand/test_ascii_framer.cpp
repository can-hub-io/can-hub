#include <cest>

#include <cstring>
#include <string>

extern "C" {
#include "socketcand/domain/ascii_framer.h"
}

static AsciiFramer framer;

static size_t pushText(const char *text)
{
    return AsciiFramer_Push(&framer, (const uint8_t *)text, strlen(text));
}

static size_t drainAll(const uint8_t *data, size_t size)
{
    char command[64];
    size_t length = 0;
    size_t offset = 0;
    size_t taken;
    size_t extracted = 0;

    while (offset < size) {
        taken = AsciiFramer_Push(&framer, data + offset, size - offset);
        offset += taken;
        while (AsciiFramer_Next(&framer, command, sizeof(command), &length)) {
            extracted++;
        }
        if (taken == 0) {
            AsciiFramer_Reset(&framer);
        }
    }

    return extracted;
}

describe("ascii_framer", []() {
    beforeEach([]() {
        AsciiFramer_Reset(&framer);
    });

    it("extracts back-to-back commands", []() {
        char command[64];
        size_t length = 0;
        bool first;
        bool first_match;
        bool second;
        bool second_match;
        bool third;

        pushText("< open can0 >< rawmode >");

        first = AsciiFramer_Next(&framer, command, sizeof(command), &length);
        first_match = strstr(command, "open can0") != nullptr;
        second = AsciiFramer_Next(&framer, command, sizeof(command), &length);
        second_match = strstr(command, "rawmode") != nullptr;
        third = AsciiFramer_Next(&framer, command, sizeof(command), &length);

        expect(first).toBe(true);
        expect(first_match).toBe(true);
        expect(second).toBe(true);
        expect(second_match).toBe(true);
        expect(third).toBe(false);
    });

    it("holds a command back until its closing bracket", []() {
        char command[64];
        size_t length = 0;
        bool found;

        pushText("< raw");

        found = AsciiFramer_Next(&framer, command, sizeof(command), &length);

        expect(found).toBe(false);
    });

    it("completes a command across pushes", []() {
        char command[64];
        size_t length = 0;
        bool found;
        bool match;

        pushText("< raw");
        AsciiFramer_Next(&framer, command, sizeof(command), &length);

        pushText("mode >");
        found = AsciiFramer_Next(&framer, command, sizeof(command), &length);
        match = strstr(command, "rawmode") != nullptr;

        expect(found).toBe(true);
        expect(match).toBe(true);
    });

    it("discards junk before a command", []() {
        char command[64];
        size_t length = 0;
        bool found;
        bool match;

        pushText("garbage< echo >");

        found = AsciiFramer_Next(&framer, command, sizeof(command), &length);
        match = strstr(command, "echo") != nullptr;

        expect(found).toBe(true);
        expect(match).toBe(true);
    });

    it("push takes only what fits the buffer", []() {
        std::string oversized(ASCII_FRAMER_BUFFER_SIZE + 100, 'x');
        size_t taken;

        taken = pushText(oversized.c_str());

        expect(taken).toBe((size_t)ASCII_FRAMER_BUFFER_SIZE);
    });

    it("extracts every command from a burst larger than the buffer", []() {
        std::string burst;
        size_t want = 0;
        size_t extracted;

        while (burst.size() < (size_t)ASCII_FRAMER_BUFFER_SIZE * 8) {
            burst += "< send 123 8 0011223344556677 >";
            want++;
        }

        extracted = drainAll((const uint8_t *)burst.data(), burst.size());

        expect(extracted).toBe(want);
    });

    it("drops a single command longer than the buffer and recovers", []() {
        std::string stream = "< ";
        size_t extracted;

        stream += std::string(ASCII_FRAMER_BUFFER_SIZE + 100, 'A');
        stream += " >< echo >";

        extracted = drainAll((const uint8_t *)stream.data(), stream.size());

        expect(extracted).toBe((size_t)1);
    });
});
