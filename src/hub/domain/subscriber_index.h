#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hub/domain/client_session.h"
#include "hub/domain/interface_registry.h"
#include "hub/domain/peer_directory.h"

#define SUBSCRIBER_INDEX_SUBSCRIBERS_MAX (PEER_DIRECTORY_MAX * CLIENT_SESSION_BINDINGS_MAX)

/*
 * Who listens to each interface: per registry slot, the chain of client
 * bindings attached to it, so routing a frame costs its subscribers instead
 * of a walk over every peer. A subscriber is one binding of one peer. The
 * index only mirrors the sessions; whoever changes a session refreshes it.
 */
typedef struct {
    uint16_t first[INTERFACE_REGISTRY_MAX];
    uint16_t last[INTERFACE_REGISTRY_MAX];
    uint16_t next[SUBSCRIBER_INDEX_SUBSCRIBERS_MAX];
    uint16_t previous[SUBSCRIBER_INDEX_SUBSCRIBERS_MAX];
    uint16_t interface_slot[SUBSCRIBER_INDEX_SUBSCRIBERS_MAX];
} SubscriberIndex;

void SubscriberIndex_Reset(SubscriberIndex *self);
void SubscriberIndex_Subscribe(SubscriberIndex *self, uint16_t interface_slot, uint16_t peer_slot, uint8_t binding_index);
void SubscriberIndex_DropPeer(SubscriberIndex *self, uint16_t peer_slot);
bool SubscriberIndex_First(const SubscriberIndex *self, uint16_t interface_slot, uint16_t *subscriber);
bool SubscriberIndex_Next(const SubscriberIndex *self, uint16_t *subscriber);
uint16_t SubscriberIndex_PeerSlot(uint16_t subscriber);
uint8_t SubscriberIndex_BindingIndex(uint16_t subscriber);
