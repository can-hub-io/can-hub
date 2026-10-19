#include <cest>

#include <cstring>

extern "C" {
#include "platform/linux/shared/log.h"
}

static char line[256];
static LogLevel parsed_level;
static bool parsed;

static void parseLevel(const char *text);

describe("Log_ParseLevel", []() {
    it("parses error", []() {
        parseLevel("error");

        expect(parsed).toBe(true);
        expect((int)parsed_level).toBe((int)kLOG_LEVEL_ERROR);
    });

    it("parses warn", []() {
        parseLevel("warn");

        expect(parsed).toBe(true);
        expect((int)parsed_level).toBe((int)kLOG_LEVEL_WARN);
    });

    it("parses info", []() {
        parseLevel("info");

        expect(parsed).toBe(true);
        expect((int)parsed_level).toBe((int)kLOG_LEVEL_INFO);
    });

    it("parses debug", []() {
        parseLevel("debug");

        expect(parsed).toBe(true);
        expect((int)parsed_level).toBe((int)kLOG_LEVEL_DEBUG);
    });

    it("rejects unknown text", []() {
        parseLevel("verbose");

        expect(parsed).toBe(false);
    });
});

describe("Log_IsEnabled", []() {
    it("hides debug at the default info level", []() {
        Log_SetLevel(kLOG_LEVEL_INFO);

        expect(Log_IsEnabled(kLOG_LEVEL_INFO)).toBe(true);
        expect(Log_IsEnabled(kLOG_LEVEL_DEBUG)).toBe(false);
    });

    it("shows everything at the debug level", []() {
        Log_SetLevel(kLOG_LEVEL_DEBUG);

        expect(Log_IsEnabled(kLOG_LEVEL_DEBUG)).toBe(true);
        expect(Log_IsEnabled(kLOG_LEVEL_ERROR)).toBe(true);
    });

    it("hides warn at the error level", []() {
        Log_SetLevel(kLOG_LEVEL_ERROR);

        expect(Log_IsEnabled(kLOG_LEVEL_ERROR)).toBe(true);
        expect(Log_IsEnabled(kLOG_LEVEL_WARN)).toBe(false);
    });
});

describe("Log_InitFromArgs", []() {
    it("applies the --log-level flag", []() {
        const char *argv[] = { "can-hub", "--log-level", "error", "status" };

        Log_InitFromArgs("can-hub", 4, (char **)argv);

        expect(Log_IsEnabled(kLOG_LEVEL_ERROR)).toBe(true);
        expect(Log_IsEnabled(kLOG_LEVEL_WARN)).toBe(false);
    });

    it("ignores an unknown --log-level value and keeps the default", []() {
        const char *argv[] = { "can-hub", "--log-level", "loud" };

        Log_SetLevel(kLOG_LEVEL_INFO);

        Log_InitFromArgs("can-hub", 3, (char **)argv);

        expect(Log_IsEnabled(kLOG_LEVEL_INFO)).toBe(true);
        expect(Log_IsEnabled(kLOG_LEVEL_DEBUG)).toBe(false);
    });
});

describe("Log_Format", []() {
    it("prefixes the component without a priority or tag for an info terminal line", []() {
        Log_Format(line, sizeof(line), false, kLOG_LEVEL_INFO, "can-hub-agent", "connected to hub");

        expect((const char *)line).toBe("can-hub-agent: connected to hub\n");
    });

    it("tags a warning in a terminal line", []() {
        Log_Format(line, sizeof(line), false, kLOG_LEVEL_WARN, "can-hub", "tcp is open");

        expect((const char *)line).toBe("can-hub: warning: tcp is open\n");
    });

    it("tags an error in a terminal line", []() {
        Log_Format(line, sizeof(line), false, kLOG_LEVEL_ERROR, "can-hub", "boom");

        expect((const char *)line).toBe("can-hub: error: boom\n");
    });

    it("emits a syslog priority prefix and tag under journald", []() {
        Log_Format(line, sizeof(line), true, kLOG_LEVEL_ERROR, "can-hub", "boom");

        expect((const char *)line).toBe("<3>can-hub: error: boom\n");
    });

    it("maps a warning to syslog priority 4", []() {
        Log_Format(line, sizeof(line), true, kLOG_LEVEL_WARN, "x", "m");

        expect((const char *)line).toBe("<4>x: warning: m\n");
    });

    it("maps info to syslog priority 6", []() {
        Log_Format(line, sizeof(line), true, kLOG_LEVEL_INFO, "x", "m");

        expect((const char *)line).toBe("<6>x: m\n");
    });

    it("maps debug to syslog priority 7", []() {
        Log_Format(line, sizeof(line), true, kLOG_LEVEL_DEBUG, "x", "m");

        expect((const char *)line).toBe("<7>x: debug: m\n");
    });
});

static void parseLevel(const char *text)
{
    parsed = Log_ParseLevel(text, &parsed_level);
}
