#include "agent/domain/tx_pacer.h"

#include <string.h>

#define DECREASE_NUMERATOR 4
#define DECREASE_DENOMINATOR 5
#define INCREASE_DIVISOR 8
#define FLOOR_DIVISOR 16

void TxPacer_Reset(TxPacer *self)
{
    memset(self, 0, sizeof(*self));
}

uint32_t TxPacer_Update(TxPacer *self, uint8_t interface_index, uint32_t advertised_rate, uint64_t tx_dropped)
{
    uint32_t floor_rate;

    if (interface_index >= REGISTER_INTERFACES_MAX) {
        return advertised_rate;
    }

    if (!self->seen[interface_index]) {
        self->seen[interface_index] = true;
        self->last_tx_dropped[interface_index] = tx_dropped;
        self->credit[interface_index] = advertised_rate;
        return advertised_rate;
    }

    if (tx_dropped > self->last_tx_dropped[interface_index]) {
        floor_rate = advertised_rate / FLOOR_DIVISOR;
        self->credit[interface_index] =
            (uint32_t)((uint64_t)self->credit[interface_index] * DECREASE_NUMERATOR / DECREASE_DENOMINATOR);
        if (self->credit[interface_index] < floor_rate) {
            self->credit[interface_index] = floor_rate;
        }
    } else {
        self->credit[interface_index] += advertised_rate / INCREASE_DIVISOR;
        if (self->credit[interface_index] > advertised_rate) {
            self->credit[interface_index] = advertised_rate;
        }
    }

    self->last_tx_dropped[interface_index] = tx_dropped;

    return self->credit[interface_index];
}
