#include "agent/domain/echo_correlator.h"

#include <string.h>

static uint8_t slotAt(const EchoCorrelator *self, uint8_t interface_index, uint8_t position);
static void dropOldest(EchoCorrelator *self, uint8_t interface_index);

/* ---------- public ---------- */

void EchoCorrelator_Reset(EchoCorrelator *self)
{
    memset(self, 0, sizeof(*self));
}

void EchoCorrelator_Push(EchoCorrelator *self, uint8_t interface_index, uint8_t token, uint32_t can_id)
{
    uint8_t slot;

    if (interface_index >= REGISTER_INTERFACES_MAX) {
        return;
    }
    if (self->count[interface_index] == ECHO_CORRELATOR_PENDING_MAX) {
        dropOldest(self, interface_index);
    }

    slot = slotAt(self, interface_index, self->count[interface_index]);
    self->tokens[interface_index][slot] = token;
    self->can_ids[interface_index][slot] = can_id;
    self->count[interface_index]++;
}

void EchoCorrelator_DropNewest(EchoCorrelator *self, uint8_t interface_index)
{
    if (interface_index >= REGISTER_INTERFACES_MAX || self->count[interface_index] == 0) {
        return;
    }

    self->count[interface_index]--;
}

bool EchoCorrelator_PopMatch(EchoCorrelator *self, uint8_t interface_index, uint32_t can_id, uint8_t *token)
{
    uint8_t position;
    uint8_t slot;

    if (interface_index >= REGISTER_INTERFACES_MAX) {
        return false;
    }

    for(position=0; position<self->count[interface_index]; position++) {
        slot = slotAt(self, interface_index, position);
        if (self->can_ids[interface_index][slot] != can_id) {
            continue;
        }

        *token = self->tokens[interface_index][slot];
        self->head[interface_index] = (uint8_t)((slot + 1) % ECHO_CORRELATOR_PENDING_MAX);
        self->count[interface_index] -= position + 1;
        return true;
    }

    return false;
}

/* ---------- private ---------- */

static uint8_t slotAt(const EchoCorrelator *self, uint8_t interface_index, uint8_t position)
{
    return (uint8_t)((self->head[interface_index] + position) % ECHO_CORRELATOR_PENDING_MAX);
}

static void dropOldest(EchoCorrelator *self, uint8_t interface_index)
{
    self->head[interface_index] = (uint8_t)((self->head[interface_index] + 1) % ECHO_CORRELATOR_PENDING_MAX);
    self->count[interface_index]--;
}
