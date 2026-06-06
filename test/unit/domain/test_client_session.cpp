#include <cest>

extern "C" {
#include "domain/client_session.h"
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

        first_opened = ClientSession_OpenInterface(&session, 7, &first_channel);
        second_opened = ClientSession_OpenInterface(&session, 9, &second_channel);

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

        ClientSession_OpenInterface(&session, 7, &channel);

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

        ClientSession_OpenInterface(&session, 7, &channel);

        ClientSession_CloseChannel(&session, channel);
        interface_found = ClientSession_InterfaceForChannel(&session, channel, &found_interface);

        expect(interface_found).toBe(false);
    });

    it("removes every binding of an interface", []() {
        uint8_t channel = 0;
        bool channel_found;
        uint8_t found_channel = 0;

        ClientSession_OpenInterface(&session, 7, &channel);

        ClientSession_RemoveInterface(&session, 7);
        channel_found = ClientSession_ChannelForInterface(&session, 7, &found_channel);

        expect(channel_found).toBe(false);
    });

    it("rejects opening beyond the binding capacity", []() {
        uint8_t channel = 0;
        bool opened = true;
        uint8_t i;

        for(i=0; i<CLIENT_SESSION_BINDINGS_MAX; i++) {
            ClientSession_OpenInterface(&session, i, &channel);
        }

        opened = ClientSession_OpenInterface(&session, 100, &channel);

        expect(opened).toBe(false);
    });
});
