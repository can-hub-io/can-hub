#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "agent/ports/can_port.h"
#include "agent/ports/transport_events.h"
#include "agent/ports/transport_port.h"
#include "protocol/frame_message.h"

typedef enum tmirror_state_e {
    kMIRROR_OPENING = 0,
    kMIRROR_PUMPING,
    kMIRROR_FAILED,
    kMIRROR_MAX,
} TMIRROR_STATE;

typedef struct {
    TransportPort *hub;
    CanPort *can;
    uint32_t interface_id;
    uint8_t channel;
    uint8_t state;
    bool can_write;
    bool pending_write;
} MirrorApp;

void MirrorApp_Init(MirrorApp *self, TransportPort *hub, CanPort *can, uint32_t interface_id);
TransportEvents MirrorApp_TransportEvents(MirrorApp *self);
void MirrorApp_OnCanFrame(MirrorApp *self, const FrameMessage *frame);
uint8_t MirrorApp_State(const MirrorApp *self);
bool MirrorApp_CanWrite(const MirrorApp *self);
