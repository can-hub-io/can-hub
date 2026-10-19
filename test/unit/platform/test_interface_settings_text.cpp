#include <cest>

#include <cstdio>
#include <cstring>
#include <string>

extern "C" {
#include "platform/linux/shared/interface_settings_text.h"
}

#define PRINTED_SIZE 1024

static InterfaceSettings settings;
static char error[INTERFACE_SETTINGS_TEXT_ERROR_SIZE];
static bool parsed;
static std::string printed;

static void parse(std::initializer_list<const char *> words);
static void print(void);

describe("interface_settings_text", []() {
    it("parses ip-link style settings into the fields they name", []() {
        parse({ "bitrate", "500000", "sample-point", "0.875", "dbitrate", "2000000", "dsample-point", "0.75" });

        expect(parsed).toBe(true);
        expect(settings.fields).toBe((uint32_t)(INTERFACE_FIELD_BITRATE | INTERFACE_FIELD_SAMPLE_POINT
            | INTERFACE_FIELD_DATA_BITRATE | INTERFACE_FIELD_DATA_SAMPLE_POINT));
        expect(settings.bitrate).toBe((uint32_t)500000);
        expect(settings.sample_point).toBe((uint16_t)875);
        expect(settings.data_bitrate).toBe((uint32_t)2000000);
        expect(settings.data_sample_point).toBe((uint16_t)750);
    });

    it("collects the control modes into one mask and their values", []() {
        parse({ "fd", "on", "listen-only", "off", "berr-reporting", "on" });

        expect(settings.fields).toBe((uint32_t)INTERFACE_FIELD_CTRLMODE);
        expect(settings.ctrlmode_mask).toBe((uint32_t)(INTERFACE_CTRLMODE_FD | INTERFACE_CTRLMODE_LISTEN_ONLY | INTERFACE_CTRLMODE_BERR_REPORTING));
        expect(settings.ctrlmode_flags).toBe((uint32_t)(INTERFACE_CTRLMODE_FD | INTERFACE_CTRLMODE_BERR_REPORTING));
    });

    it("takes up and down as the link state", []() {
        parse({ "restart-ms", "100", "txqueuelen", "1000", "down" });

        expect(parsed).toBe(true);
        expect(settings.fields).toBe((uint32_t)(INTERFACE_FIELD_RESTART_MS | INTERFACE_FIELD_TXQUEUELEN | INTERFACE_FIELD_LINK));
        expect(settings.link_up).toBe(false);
        expect(settings.restart_ms).toBe((uint32_t)100);
        expect(settings.txqueuelen).toBe((uint32_t)1000);
    });

    it("rejects a sample point that is not a fraction below one", []() {
        parse({ "sample-point", "87.5" });

        expect(parsed).toBe(false);
        expect(std::string(error)).toBe(std::string("invalid sample-point 87.5"));
    });

    it("rejects a bitrate that is not a whole number", []() {
        parse({ "bitrate", "500k" });

        expect(parsed).toBe(false);
    });

    it("rejects a control mode value other than on or off", []() {
        parse({ "fd", "yes" });

        expect(std::string(error)).toBe(std::string("fd takes on or off"));
    });

    it("rejects a setting with no value", []() {
        parse({ "bitrate" });

        expect(std::string(error)).toBe(std::string("bitrate needs a value"));
    });

    it("rejects an unknown setting", []() {
        parse({ "termination", "120" });

        expect(std::string(error)).toBe(std::string("unknown setting termination"));
    });

    it("prints one line per field the settings carry", []() {
        parse({ "bitrate", "500000", "sample-point", "0.875", "fd", "on", "up" });
        print();

        expect(printed).toBe(std::string(
            "link             up\n"
            "bitrate          500000\n"
            "sample-point     0.875\n"
            "fd               on\n"));
    });
});

static void parse(std::initializer_list<const char *> words)
{
    const char *list[16];
    uint8_t count = 0;

    for(const char *word : words) {
        list[count++] = word;
    }

    parsed = InterfaceSettingsText_Parse(&settings, list, count, error);
}

static void print(void)
{
    char buffer[PRINTED_SIZE];
    FILE *stream = fmemopen(buffer, sizeof(buffer), "w");

    memset(buffer, 0, sizeof(buffer));
    InterfaceSettingsText_Print(stream, &settings);
    fclose(stream);
    printed = buffer;
}
