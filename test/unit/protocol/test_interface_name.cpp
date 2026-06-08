#include <cest>

#include <cstdio>
#include <cstring>

extern "C" {
#include "protocol/interface_name.h"
#include "protocol/list_message.h"
}

static ListReplyMessage reply;

static void addEntry(const char *agent, const char *iface, uint32_t interface_id);

describe("interface_name", []() {
    beforeEach([]() {
        memset(&reply, 0, sizeof(reply));
    });

    it("treats a slashed target as namespaced", []() {
        expect(InterfaceName_IsNamespaced("truck42/can0")).toBe(true);
    });

    it("treats a bare number as not namespaced", []() {
        expect(InterfaceName_IsNamespaced("7")).toBe(false);
    });

    it("finds the interface id of a namespaced name", []() {
        uint32_t interface_id = 0;
        bool found;

        addEntry("truck42", "can0", 3);
        addEntry("truck42", "can1", 9);
        found = InterfaceName_Find(&reply, "truck42/can1", &interface_id);

        expect(found).toBe(true);
        expect(interface_id).toBe((uint32_t)9);
    });

    it("misses a name absent from the page", []() {
        uint32_t interface_id = 0;
        bool found;

        addEntry("truck42", "can0", 3);
        found = InterfaceName_Find(&reply, "truck42/can9", &interface_id);

        expect(found).toBe(false);
    });
});

static void addEntry(const char *agent, const char *iface, uint32_t interface_id)
{
    reply.entries[reply.count].interface_id = interface_id;
    snprintf(reply.entries[reply.count].agent_name, REGISTER_AGENT_NAME_SIZE, "%s", agent);
    snprintf(reply.entries[reply.count].interface_name, REGISTER_INTERFACE_NAME_SIZE, "%s", iface);
    reply.count++;
}
