#include <cest>

#include <cstring>

extern "C" {
#include "socketcand/domain/interface_catalogue.h"
}

static InterfaceCatalogue catalogue;

static void setEntry(ListReplyEntry *entry, uint32_t id, const char *agent, const char *iface)
{
    memset(entry, 0, sizeof(*entry));
    entry->interface_id = id;
    strncpy(entry->agent_name, agent, REGISTER_AGENT_NAME_SIZE - 1);
    strncpy(entry->interface_name, iface, REGISTER_INTERFACE_NAME_SIZE - 1);
}

describe("interface_catalogue", []() {
    beforeEach([]() {
        InterfaceCatalogue_Reset(&catalogue);
    });

    it("composes namespaced names and resolves them", []() {
        ListReplyMessage reply;
        uint32_t id = 0;
        bool found;

        memset(&reply, 0, sizeof(reply));
        reply.count = 2;
        setEntry(&reply.entries[0], 7, "truck42", "can0");
        setEntry(&reply.entries[1], 9, "truck42", "can1");

        InterfaceCatalogue_AppendPage(&catalogue, &reply);
        found = InterfaceCatalogue_FindByName(&catalogue, "truck42/can0", &id);

        expect(InterfaceCatalogue_Count(&catalogue)).toBe(2);
        expect(found).toBe(true);
        expect(id).toBe((uint32_t)7);
        expect(strcmp(InterfaceCatalogue_At(&catalogue, 1)->name, "truck42/can1") == 0).toBe(true);
    });

    it("does not resolve an unknown bus", []() {
        ListReplyMessage reply;
        uint32_t id = 0;

        memset(&reply, 0, sizeof(reply));
        reply.count = 1;
        setEntry(&reply.entries[0], 7, "truck42", "can0");
        InterfaceCatalogue_AppendPage(&catalogue, &reply);

        expect(InterfaceCatalogue_FindByName(&catalogue, "truck42/can9", &id)).toBe(false);
    });

    it("clears on reset", []() {
        ListReplyMessage reply;

        memset(&reply, 0, sizeof(reply));
        reply.count = 1;
        setEntry(&reply.entries[0], 7, "truck42", "can0");
        InterfaceCatalogue_AppendPage(&catalogue, &reply);
        InterfaceCatalogue_Reset(&catalogue);

        expect(InterfaceCatalogue_Count(&catalogue)).toBe(0);
    });
});
