#include "domain/channel_map.h"

#include <string.h>

/* ---------- public ---------- */

void ChannelMap_Reset(ChannelMap *self)
{
    memset(self, 0, sizeof(*self));
}

bool ChannelMap_AssignFromAck(ChannelMap *self, const RegisterAckMessage *ack)
{
    if (ack->status != REGISTER_STATUS_OK || ack->interface_count > REGISTER_INTERFACES_MAX) {
        return false;
    }

    self->interface_count = ack->interface_count;
    memcpy(self->channels, ack->channels, ack->interface_count);

    return true;
}

bool ChannelMap_ChannelForInterface(const ChannelMap *self, uint8_t interface_index, uint8_t *channel)
{
    if (interface_index >= self->interface_count) {
        return false;
    }

    *channel = self->channels[interface_index];

    return true;
}

bool ChannelMap_InterfaceForChannel(const ChannelMap *self, uint8_t channel, uint8_t *interface_index)
{
    uint8_t i;

    for(i=0; i<self->interface_count; i++) {
        if (self->channels[i] == channel) {
            *interface_index = i;
            return true;
        }
    }

    return false;
}
