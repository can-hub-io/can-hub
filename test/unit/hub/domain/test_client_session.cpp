#include <cest>

extern "C" {
#include "hub/domain/client_session.h"
}

static ClientSession session;

describe("client_session", []() {
    beforeEach([]() {
        ClientSession_Reset(&session);
    });

    it("opens an interface under a unique channel", []() {
        uint8_t first_channel = 0;
        uint8_t second_channel = 0;
        bool first_opened;
        bool second_opened;

        first_opened = ClientSession_OpenInterface(&session, 7, false, false, &first_channel);
        second_opened = ClientSession_OpenInterface(&session, 9, false, false, &second_channel);

        expect(first_opened).toBe(true);
        expect(second_opened).toBe(true);
        expect(first_channel == second_channel).toBe(false);
    });

    it("maps channels to interfaces in both directions", []() {
        uint8_t channel = 0;
        uint8_t found_channel = 0;
        uint32_t found_interface = 0;
        bool channel_found;
        bool interface_found;

        ClientSession_OpenInterface(&session, 7, false, false, &channel);

        interface_found = ClientSession_InterfaceForChannel(&session, channel, &found_interface);
        channel_found = ClientSession_ChannelForInterface(&session, 7, &found_channel);

        expect(interface_found).toBe(true);
        expect(found_interface).toBe((uint32_t)7);
        expect(channel_found).toBe(true);
        expect(found_channel).toBe(channel);
    });

    it("forgets a closed channel", []() {
        uint8_t channel = 0;
        uint32_t found_interface = 0;
        bool interface_found;

        ClientSession_OpenInterface(&session, 7, false, false, &channel);

        ClientSession_CloseChannel(&session, channel);
        interface_found = ClientSession_InterfaceForChannel(&session, channel, &found_interface);

        expect(interface_found).toBe(false);
    });

    it("iterates every binding of an interface", []() {
        uint8_t first_channel = 0;
        uint8_t second_channel = 0;
        uint8_t other_channel = 0;
        uint8_t iterator = 0;
        const ChannelBinding *first_binding;
        const ChannelBinding *second_binding;
        const ChannelBinding *end_binding;

        ClientSession_OpenInterface(&session, 7, false, false, &first_channel);
        ClientSession_OpenInterface(&session, 9, false, false, &other_channel);
        ClientSession_OpenInterface(&session, 7, false, false, &second_channel);

        first_binding = ClientSession_NextBindingForInterface(&session, 7, &iterator);
        second_binding = ClientSession_NextBindingForInterface(&session, 7, &iterator);
        end_binding = ClientSession_NextBindingForInterface(&session, 7, &iterator);

        expect(first_binding->channel).toBe(first_channel);
        expect(second_binding->channel).toBe(second_channel);
        expect(end_binding == NULL).toBe(true);
    });

    it("iterates nothing for an interface that is not open", []() {
        uint8_t channel = 0;
        uint8_t iterator = 0;
        const ChannelBinding *binding;

        ClientSession_OpenInterface(&session, 7, false, false, &channel);

        binding = ClientSession_NextBindingForInterface(&session, 9, &iterator);

        expect(binding == NULL).toBe(true);
    });

    it("removes every binding of an interface", []() {
        uint8_t channel = 0;
        bool channel_found;
        uint8_t found_channel = 0;

        ClientSession_OpenInterface(&session, 7, false, false, &channel);

        ClientSession_RemoveInterface(&session, 7);
        channel_found = ClientSession_ChannelForInterface(&session, 7, &found_channel);

        expect(channel_found).toBe(false);
    });

    it("accepts every frame on a channel with no filters", []() {
        uint8_t channel = 0;

        ClientSession_OpenInterface(&session, 7, false, false, &channel);

        expect(ClientSession_ChannelAccepts(&session, channel, 0x123)).toBe(true);
        expect(ClientSession_ChannelAccepts(&session, channel, 0x7AB)).toBe(true);
    });

    it("accepts only frames matching a mask filter", []() {
        uint8_t channel = 0;
        CanFilter filters[1] = { { 0x100, 0x700 } };
        bool set_ok;

        ClientSession_OpenInterface(&session, 7, false, false, &channel);
        set_ok = ClientSession_SetFilters(&session, channel, filters, 1);

        expect(set_ok).toBe(true);
        expect(ClientSession_ChannelAccepts(&session, channel, 0x123)).toBe(true);
        expect(ClientSession_ChannelAccepts(&session, channel, 0x1FF)).toBe(true);
        expect(ClientSession_ChannelAccepts(&session, channel, 0x223)).toBe(false);
    });

    it("accepts a frame matching any filter in the list", []() {
        uint8_t channel = 0;
        CanFilter filters[2] = { { 0x100, 0x7FF }, { 0x200, 0x7FF } };

        ClientSession_OpenInterface(&session, 7, false, false, &channel);
        ClientSession_SetFilters(&session, channel, filters, 2);

        expect(ClientSession_ChannelAccepts(&session, channel, 0x100)).toBe(true);
        expect(ClientSession_ChannelAccepts(&session, channel, 0x200)).toBe(true);
        expect(ClientSession_ChannelAccepts(&session, channel, 0x300)).toBe(false);
    });

    it("clears filters back to pass-all on an empty set", []() {
        uint8_t channel = 0;
        CanFilter filters[1] = { { 0x100, 0x7FF } };

        ClientSession_OpenInterface(&session, 7, false, false, &channel);
        ClientSession_SetFilters(&session, channel, filters, 1);
        ClientSession_SetFilters(&session, channel, NULL, 0);

        expect(ClientSession_ChannelAccepts(&session, channel, 0x300)).toBe(true);
    });

    it("rejects setting filters on an unopened channel", []() {
        CanFilter filters[1] = { { 0x100, 0x7FF } };
        bool set_ok;

        set_ok = ClientSession_SetFilters(&session, 42, filters, 1);

        expect(set_ok).toBe(false);
    });

    it("rejects opening beyond the binding capacity", []() {
        uint8_t channel = 0;
        bool opened = true;
        uint8_t i;

        for(i=0; i<CLIENT_SESSION_BINDINGS_MAX; i++) {
            ClientSession_OpenInterface(&session, i, false, false, &channel);
        }

        opened = ClientSession_OpenInterface(&session, 100, false, false, &channel);

        expect(opened).toBe(false);
    });
});
