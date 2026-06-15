#include <cest>

extern "C" {
#include "hub/domain/interface_registry.h"
}

static InterfaceRegistry registry;
static const RegisterMessage truck_registration = { "truck42", 2, { "can0", "can1" } };

describe("interface_registry", []() {
    beforeEach([]() {
        InterfaceRegistry_Reset(&registry);
    });

    it("registers an agent and assigns sequential channels", []() {
        RegisterAckMessage ack;
        bool registered;

        registered = InterfaceRegistry_RegisterAgent(&registry, 100, &truck_registration, &ack);

        expect(registered).toBe(true);
        expect(ack.status).toBe(REGISTER_STATUS_OK);
        expect(ack.interface_count).toBe(2);
        expect(ack.channels[0]).toBe(0);
        expect(ack.channels[1]).toBe(1);
    });

    it("rejects a colliding agent and interface name pair", []() {
        RegisterAckMessage first_ack;
        RegisterAckMessage second_ack;
        bool second_registered;

        InterfaceRegistry_RegisterAgent(&registry, 100, &truck_registration, &first_ack);

        second_registered = InterfaceRegistry_RegisterAgent(&registry, 200, &truck_registration, &second_ack);

        expect(second_registered).toBe(false);
        expect(second_ack.status).toBe(1);
    });

    it("accepts the same interface names under another agent name", []() {
        RegisterMessage other_registration = { "truck43", 1, { "can0" } };
        RegisterAckMessage ack;
        bool registered;

        InterfaceRegistry_RegisterAgent(&registry, 100, &truck_registration, &ack);

        registered = InterfaceRegistry_RegisterAgent(&registry, 200, &other_registration, &ack);

        expect(registered).toBe(true);
    });

    it("finds entries by id and by agent channel", []() {
        RegisterAckMessage ack;
        const InterfaceEntry *by_channel;
        const InterfaceEntry *by_id;

        InterfaceRegistry_RegisterAgent(&registry, 100, &truck_registration, &ack);

        by_channel = InterfaceRegistry_FindByAgentChannel(&registry, 100, 1);
        by_id = InterfaceRegistry_FindById(&registry, by_channel->interface_id);

        expect(by_channel).toBeNotNull();
        expect(by_id).toBe(by_channel);
        expect((const char *)by_channel->interface_name).toBe("can1");
    });

    it("stores the latest tx-drop counter by agent channel", []() {
        RegisterAckMessage ack;
        const InterfaceEntry *entry;

        InterfaceRegistry_RegisterAgent(&registry, 100, &truck_registration, &ack);

        InterfaceRegistry_SetTxDropped(&registry, 100, 1, 7);
        InterfaceRegistry_SetTxDropped(&registry, 100, 1, 25);
        entry = InterfaceRegistry_FindByAgentChannel(&registry, 100, 1);

        expect(entry->tx_dropped).toBe((uint64_t)25);
    });

    it("leaves a quiet channel at zero when another reports drops", []() {
        RegisterAckMessage ack;
        const InterfaceEntry *quiet;

        InterfaceRegistry_RegisterAgent(&registry, 100, &truck_registration, &ack);

        InterfaceRegistry_SetTxDropped(&registry, 100, 1, 99);
        quiet = InterfaceRegistry_FindByAgentChannel(&registry, 100, 0);

        expect(quiet->tx_dropped).toBe((uint64_t)0);
    });

    it("frees the names after the agent is removed", []() {
        RegisterAckMessage ack;
        bool registered_again;

        InterfaceRegistry_RegisterAgent(&registry, 100, &truck_registration, &ack);

        InterfaceRegistry_RemovePeer(&registry, 100);
        registered_again = InterfaceRegistry_RegisterAgent(&registry, 200, &truck_registration, &ack);

        expect(registered_again).toBe(true);
    });

    it("never reuses interface ids", []() {
        RegisterAckMessage ack;
        const InterfaceEntry *first_entry;
        const InterfaceEntry *second_entry;
        uint32_t first_id;

        InterfaceRegistry_RegisterAgent(&registry, 100, &truck_registration, &ack);
        first_entry = InterfaceRegistry_FindByAgentChannel(&registry, 100, 0);
        first_id = first_entry->interface_id;

        InterfaceRegistry_RemovePeer(&registry, 100);
        InterfaceRegistry_RegisterAgent(&registry, 200, &truck_registration, &ack);
        second_entry = InterfaceRegistry_FindByAgentChannel(&registry, 200, 0);

        expect(second_entry->interface_id > first_id).toBe(true);
    });

    it("lists registered interfaces with pagination metadata", []() {
        RegisterAckMessage ack;
        ListReplyMessage reply;

        InterfaceRegistry_RegisterAgent(&registry, 100, &truck_registration, &ack);

        InterfaceRegistry_List(&registry, 0, &reply);

        expect(reply.count).toBe(2);
        expect(reply.flags).toBe(0);
        expect((const char *)reply.entries[0].agent_name).toBe("truck42");
        expect((const char *)reply.entries[0].interface_name).toBe("can0");
        expect((const char *)reply.entries[1].interface_name).toBe("can1");
    });

    it("paginates past the first page", []() {
        RegisterAckMessage ack;
        ListReplyMessage reply;

        InterfaceRegistry_RegisterAgent(&registry, 100, &truck_registration, &ack);

        InterfaceRegistry_List(&registry, 1, &reply);

        expect(reply.count).toBe(1);
        expect((const char *)reply.entries[0].interface_name).toBe("can1");
    });
});
