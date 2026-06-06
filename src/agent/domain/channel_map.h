#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "protocol/register_message.h"

typedef struct {
    uint8_t interface_count;
    uint8_t channels[REGISTER_INTERFACES_MAX];
} ChannelMap;

void ChannelMap_Reset(ChannelMap *self);
bool ChannelMap_AssignFromAck(ChannelMap *self, const RegisterAckMessage *ack);
bool ChannelMap_ChannelForInterface(const ChannelMap *self, uint8_t interface_index, uint8_t *channel);
bool ChannelMap_InterfaceForChannel(const ChannelMap *self, uint8_t channel, uint8_t *interface_index);
