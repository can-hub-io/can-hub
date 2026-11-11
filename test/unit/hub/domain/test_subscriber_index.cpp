#include <cest>

extern "C" {
#include "hub/domain/subscriber_index.h"
}

#define INTERFACE_SLOT 3
#define OTHER_INTERFACE_SLOT 4
#define SUBSCRIBERS_MAX 4

static SubscriberIndex subscribers;
static uint16_t peer_slots[SUBSCRIBERS_MAX];
static uint8_t binding_indexes[SUBSCRIBERS_MAX];

static uint16_t collectSubscribers(uint16_t interface_slot);

describe("subscriber_index", []() {
    beforeEach([]() {
        SubscriberIndex_Reset(&subscribers);
    });

    it("has no subscriber for an interface nobody opened", []() {
        uint16_t count;

        count = collectSubscribers(INTERFACE_SLOT);

        expect(count).toBe((uint16_t)0);
    });

    it("lists the subscribers of an interface in the order they subscribed", []() {
        uint16_t count;

        SubscriberIndex_Subscribe(&subscribers, INTERFACE_SLOT, 9, 2);
        SubscriberIndex_Subscribe(&subscribers, OTHER_INTERFACE_SLOT, 9, 3);
        SubscriberIndex_Subscribe(&subscribers, INTERFACE_SLOT, 5, 0);

        count = collectSubscribers(INTERFACE_SLOT);

        expect(count).toBe((uint16_t)2);
        expect(peer_slots[0]).toBe((uint16_t)9);
        expect(binding_indexes[0]).toBe((uint8_t)2);
        expect(peer_slots[1]).toBe((uint16_t)5);
        expect(binding_indexes[1]).toBe((uint8_t)0);
    });

    it("moves a binding subscribed again to its new interface", []() {
        uint16_t old_count;
        uint16_t new_count;

        SubscriberIndex_Subscribe(&subscribers, INTERFACE_SLOT, 9, 2);
        SubscriberIndex_Subscribe(&subscribers, OTHER_INTERFACE_SLOT, 9, 2);

        old_count = collectSubscribers(INTERFACE_SLOT);
        new_count = collectSubscribers(OTHER_INTERFACE_SLOT);

        expect(old_count).toBe((uint16_t)0);
        expect(new_count).toBe((uint16_t)1);
    });

    it("drops every binding of a peer and keeps the other peers", []() {
        uint16_t count;

        SubscriberIndex_Subscribe(&subscribers, INTERFACE_SLOT, 5, 0);
        SubscriberIndex_Subscribe(&subscribers, INTERFACE_SLOT, 9, 1);
        SubscriberIndex_Subscribe(&subscribers, INTERFACE_SLOT, 9, 2);
        SubscriberIndex_Subscribe(&subscribers, INTERFACE_SLOT, 7, 0);

        SubscriberIndex_DropPeer(&subscribers, 9);
        count = collectSubscribers(INTERFACE_SLOT);

        expect(count).toBe((uint16_t)2);
        expect(peer_slots[0]).toBe((uint16_t)5);
        expect(peer_slots[1]).toBe((uint16_t)7);
    });
});

static uint16_t collectSubscribers(uint16_t interface_slot)
{
    uint16_t subscriber;
    uint16_t count = 0;
    bool pending;

    pending = SubscriberIndex_First(&subscribers, interface_slot, &subscriber);
    while (pending && count < SUBSCRIBERS_MAX) {
        peer_slots[count] = SubscriberIndex_PeerSlot(subscriber);
        binding_indexes[count] = SubscriberIndex_BindingIndex(subscriber);
        count++;
        pending = SubscriberIndex_Next(&subscribers, &subscriber);
    }

    return count;
}
