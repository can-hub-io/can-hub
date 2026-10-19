#include <cest>

#include <cstdio>
#include <cstring>

extern "C" {
#include "platform/linux/shared/pin_store.h"
}

#define PIN_STORE_PATH "/tmp/can_hub_test_pin_store"
#define HUB_FINGERPRINT "ab12cd34ab12cd34ab12cd34ab12cd34ab12cd34ab12cd34ab12cd34ab12cd34"
#define OTHER_FINGERPRINT "0000000000000000000000000000000000000000000000000000000000000000"
#define LINE_BUFFER_SIZE 2048

static char key[PIN_STORE_KEY_MAX];
static char fingerprint[PIN_STORE_FINGERPRINT_HEX_SIZE];
static bool found;

static void writeStore(const char *content);
static uint32_t storeLineCount(void);
static void lookup(const char *pin_key);

describe("PinStore_HubKey", []() {
    it("lowercases the host and appends the port", []() {
        PinStore_HubKey("Hub.Example.COM", "7227", key, sizeof(key));

        expect((const char *)key).toBe("hub.example.com:7227");
    });

    it("drops the trailing dot of a fully qualified name", []() {
        PinStore_HubKey("hub.example.com.", "7227", key, sizeof(key));

        expect((const char *)key).toBe("hub.example.com:7227");
    });
});

describe("PinStore_ParseFingerprint", []() {
    it("accepts 64 hex digits and lowercases them", []() {
        bool parsed = PinStore_ParseFingerprint("AB12CD34AB12CD34AB12CD34AB12CD34AB12CD34AB12CD34AB12CD34AB12CD34", fingerprint);

        expect(parsed).toBe(true);
        expect((const char *)fingerprint).toBe(HUB_FINGERPRINT);
    });

    it("rejects a fingerprint of the wrong length", []() {
        expect(PinStore_ParseFingerprint("ab12cd34", fingerprint)).toBe(false);
    });

    it("rejects a fingerprint with a non-hex digit", []() {
        expect(PinStore_ParseFingerprint("zb12cd34ab12cd34ab12cd34ab12cd34ab12cd34ab12cd34ab12cd34ab12cd34", fingerprint)).toBe(false);
    });
});

describe("PinStore_Lookup", []() {
    beforeEach([]() {
        std::remove(PIN_STORE_PATH);
    });

    it("finds a key in a single-address entry", []() {
        writeStore("hub.example.com:7227 " HUB_FINGERPRINT "\n");

        lookup("hub.example.com:7227");

        expect(found).toBe(true);
        expect((const char *)fingerprint).toBe(HUB_FINGERPRINT);
    });

    it("finds every address of a multi-address entry", []() {
        writeStore("hub.example.com:7227,203.0.113.5:7227 " HUB_FINGERPRINT "\n");

        lookup("203.0.113.5:7227");

        expect(found).toBe(true);
        expect((const char *)fingerprint).toBe(HUB_FINGERPRINT);
    });

    it("does not match a key that only prefixes an address", []() {
        writeStore("hub.example.com:72270 " HUB_FINGERPRINT "\n");

        lookup("hub.example.com:7227");

        expect(found).toBe(false);
    });
});

describe("PinStore_Pin", []() {
    beforeEach([]() {
        std::remove(PIN_STORE_PATH);
    });

    it("adds a new address to the entry of a pinned fingerprint", []() {
        writeStore("hub.example.com:7227 " HUB_FINGERPRINT "\n");

        PinStore_Pin(PIN_STORE_PATH, "203.0.113.5:7227", HUB_FINGERPRINT);
        lookup("203.0.113.5:7227");

        expect(found).toBe(true);
        expect(storeLineCount()).toBe((uint32_t)1);
    });

    it("appends an entry for a fingerprint not pinned yet", []() {
        writeStore("hub.example.com:7227 " HUB_FINGERPRINT "\n");

        PinStore_Pin(PIN_STORE_PATH, "other.example.com:7227", OTHER_FINGERPRINT);
        lookup("other.example.com:7227");

        expect(found).toBe(true);
        expect((const char *)fingerprint).toBe(OTHER_FINGERPRINT);
        expect(storeLineCount()).toBe((uint32_t)2);
    });

    it("keeps the other entries when it adds an address", []() {
        writeStore("other.example.com:7227 " OTHER_FINGERPRINT "\nhub.example.com:7227 " HUB_FINGERPRINT "\n");

        PinStore_Pin(PIN_STORE_PATH, "203.0.113.5:7227", HUB_FINGERPRINT);
        lookup("other.example.com:7227");

        expect(found).toBe(true);
        expect((const char *)fingerprint).toBe(OTHER_FINGERPRINT);
    });
});

describe("PinStore_KnowsFingerprint", []() {
    it("knows a fingerprint pinned under any address", []() {
        writeStore("hub.example.com:7227 " HUB_FINGERPRINT "\n");

        expect(PinStore_KnowsFingerprint(PIN_STORE_PATH, HUB_FINGERPRINT)).toBe(true);
        expect(PinStore_KnowsFingerprint(PIN_STORE_PATH, OTHER_FINGERPRINT)).toBe(false);
    });
});

describe("PinStore_IsEmpty", []() {
    beforeEach([]() {
        std::remove(PIN_STORE_PATH);
    });

    it("is empty when the store does not exist", []() {
        expect(PinStore_IsEmpty(PIN_STORE_PATH)).toBe(true);
    });

    it("is not empty once a hub is pinned", []() {
        writeStore("hub.example.com:7227 " HUB_FINGERPRINT "\n");

        expect(PinStore_IsEmpty(PIN_STORE_PATH)).toBe(false);
    });
});

static void writeStore(const char *content)
{
    FILE *file = std::fopen(PIN_STORE_PATH, "w");

    std::fputs(content, file);
    std::fclose(file);
}

static uint32_t storeLineCount(void)
{
    char line[LINE_BUFFER_SIZE];
    uint32_t count = 0;
    FILE *file = std::fopen(PIN_STORE_PATH, "r");

    while (std::fgets(line, sizeof(line), file) != NULL) {
        count++;
    }
    std::fclose(file);

    return count;
}

static void lookup(const char *pin_key)
{
    found = PinStore_Lookup(PIN_STORE_PATH, pin_key, fingerprint);
}
