#include <cest>

extern "C" {
#include "agent/domain/channel_map.h"
}

#define REGISTERED_COUNT 2

static ChannelMap map;

describe("channel_map", []() {
    beforeEach([]() {
        ChannelMap_Reset(&map);
    });

    it("maps interfaces to channels from a register ack", []() {
        RegisterAckMessage ack = { REGISTER_STATUS_OK, 2, { 7, 9 } };
        bool assigned;
        uint8_t first_channel = 0;
        uint8_t second_channel = 0;
        bool first_found;
        bool second_found;

        assigned = ChannelMap_AssignFromAck(&map, &ack, ack.interface_count);
        first_found = ChannelMap_ChannelForInterface(&map, 0, &first_channel);
        second_found = ChannelMap_ChannelForInterface(&map, 1, &second_channel);

        expect(assigned).toBe(true);
        expect(first_found).toBe(true);
        expect(second_found).toBe(true);
        expect(first_channel).toBe(7);
        expect(second_channel).toBe(9);
    });

    it("resolves the interface that owns a channel", []() {
        RegisterAckMessage ack = { REGISTER_STATUS_OK, 2, { 7, 9 } };
        uint8_t interface_index = 0;
        bool found;

        ChannelMap_AssignFromAck(&map, &ack, ack.interface_count);
        found = ChannelMap_InterfaceForChannel(&map, 9, &interface_index);

        expect(found).toBe(true);
        expect(interface_index).toBe(1);
    });

    it("rejects an ack with an error status", []() {
        RegisterAckMessage ack = { 1, 2, { 7, 9 } };
        bool assigned;

        assigned = ChannelMap_AssignFromAck(&map, &ack, ack.interface_count);

        expect(assigned).toBe(false);
    });

    it("rejects an ack that assigns more interfaces than were registered", []() {
        RegisterAckMessage ack = { REGISTER_STATUS_OK, 3, { 7, 9, 11 } };
        uint8_t interface_index = 0;
        bool assigned;
        bool found;

        assigned = ChannelMap_AssignFromAck(&map, &ack, REGISTERED_COUNT);
        found = ChannelMap_InterfaceForChannel(&map, 11, &interface_index);

        expect(assigned).toBe(false);
        expect(found).toBe(false);
    });

    it("rejects an ack that assigns fewer interfaces than were registered", []() {
        RegisterAckMessage ack = { REGISTER_STATUS_OK, 1, { 7 } };
        bool assigned;

        assigned = ChannelMap_AssignFromAck(&map, &ack, REGISTERED_COUNT);

        expect(assigned).toBe(false);
    });

    it("does not resolve unknown interfaces or channels", []() {
        RegisterAckMessage ack = { REGISTER_STATUS_OK, 1, { 7 } };
        uint8_t channel = 0;
        uint8_t interface_index = 0;
        bool interface_found;
        bool channel_found;

        ChannelMap_AssignFromAck(&map, &ack, ack.interface_count);
        interface_found = ChannelMap_ChannelForInterface(&map, 1, &channel);
        channel_found = ChannelMap_InterfaceForChannel(&map, 42, &interface_index);

        expect(interface_found).toBe(false);
        expect(channel_found).toBe(false);
    });
});
