#include "hub/domain/subscriber_index.h"

#include <string.h>

#define NO_SUBSCRIBER 0xFFFF
#define NO_INTERFACE 0xFFFF

static uint16_t subscriberOf(uint16_t peer_slot, uint8_t binding_index);
static void unsubscribe(SubscriberIndex *self, uint16_t subscriber);

/* ---------- public ---------- */

void SubscriberIndex_Reset(SubscriberIndex *self)
{
    memset(self, 0xFF, sizeof(*self));
}

void SubscriberIndex_Subscribe(SubscriberIndex *self, uint16_t interface_slot, uint16_t peer_slot, uint8_t binding_index)
{
    uint16_t subscriber = subscriberOf(peer_slot, binding_index);
    uint16_t tail;

    unsubscribe(self, subscriber);

    tail = self->last[interface_slot];
    self->interface_slot[subscriber] = interface_slot;
    self->previous[subscriber] = tail;
    self->next[subscriber] = NO_SUBSCRIBER;
    self->last[interface_slot] = subscriber;
    if (tail == NO_SUBSCRIBER) {
        self->first[interface_slot] = subscriber;
        return;
    }
    self->next[tail] = subscriber;
}

void SubscriberIndex_DropPeer(SubscriberIndex *self, uint16_t peer_slot)
{
    uint8_t binding_index;

    for(binding_index=0; binding_index<CLIENT_SESSION_BINDINGS_MAX; binding_index++) {
        unsubscribe(self, subscriberOf(peer_slot, binding_index));
    }
}

bool SubscriberIndex_First(const SubscriberIndex *self, uint16_t interface_slot, uint16_t *subscriber)
{
    *subscriber = self->first[interface_slot];

    return *subscriber != NO_SUBSCRIBER;
}

bool SubscriberIndex_Next(const SubscriberIndex *self, uint16_t *subscriber)
{
    *subscriber = self->next[*subscriber];

    return *subscriber != NO_SUBSCRIBER;
}

uint16_t SubscriberIndex_PeerSlot(uint16_t subscriber)
{
    return (uint16_t)(subscriber / CLIENT_SESSION_BINDINGS_MAX);
}

uint8_t SubscriberIndex_BindingIndex(uint16_t subscriber)
{
    return (uint8_t)(subscriber % CLIENT_SESSION_BINDINGS_MAX);
}

/* ---------- private ---------- */

static uint16_t subscriberOf(uint16_t peer_slot, uint8_t binding_index)
{
    return (uint16_t)(peer_slot * CLIENT_SESSION_BINDINGS_MAX + binding_index);
}

static void unsubscribe(SubscriberIndex *self, uint16_t subscriber)
{
    uint16_t interface_slot = self->interface_slot[subscriber];
    uint16_t before = self->previous[subscriber];
    uint16_t after = self->next[subscriber];

    if (interface_slot == NO_INTERFACE) {
        return;
    }

    if (before == NO_SUBSCRIBER) {
        self->first[interface_slot] = after;
    } else {
        self->next[before] = after;
    }
    if (after == NO_SUBSCRIBER) {
        self->last[interface_slot] = before;
    } else {
        self->previous[after] = before;
    }

    self->interface_slot[subscriber] = NO_INTERFACE;
    self->previous[subscriber] = NO_SUBSCRIBER;
    self->next[subscriber] = NO_SUBSCRIBER;
}
