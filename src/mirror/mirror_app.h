#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "agent/ports/can_port.h"
#include "agent/ports/transport_events.h"
#include "agent/ports/transport_port.h"
#include "protocol/frame_message.h"
#include "protocol/hello_message.h"
#include "protocol/interface_name.h"

typedef enum tmirror_state_e {
    kMIRROR_RESOLVING = 0,
    kMIRROR_OPENING,
    kMIRROR_PUMPING,
    kMIRROR_FAILED,
    kMIRROR_MAX,
} TMIRROR_STATE;

typedef struct {
    TransportPort *hub;
    CanPort *can;
    uint32_t interface_id;
    char interface_name[INTERFACE_NAME_NAMESPACED_SIZE];
    char name[HELLO_NAME_SIZE];
    uint16_t list_offset;
    uint8_t channel;
    uint8_t state;
    bool can_write;
    bool pending_write;
    bool reliable;
} MirrorApp;

void MirrorApp_Init(MirrorApp *self, TransportPort *hub, CanPort *can, uint32_t interface_id, const char *interface_name);
void MirrorApp_SetName(MirrorApp *self, const char *name);
void MirrorApp_SetReliable(MirrorApp *self, bool reliable);
TransportEvents MirrorApp_TransportEvents(MirrorApp *self);
void MirrorApp_OnCanFrame(MirrorApp *self, const FrameMessage *frame);
uint8_t MirrorApp_State(const MirrorApp *self);
bool MirrorApp_CanWrite(const MirrorApp *self);
