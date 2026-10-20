#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "agent/ports/can_port.h"
#include "agent/ports/transport_events.h"
#include "agent/ports/transport_port.h"
#include "protocol/frame_message.h"
#include "protocol/hello_message.h"
#include "protocol/interface_name.h"
#include "shared/reconnect_backoff.h"

#define MIRROR_INTERFACE_RETRY_MS 1000

typedef enum tmirror_state_e {
    kMIRROR_RESOLVING = 0,
    kMIRROR_OPENING,
    kMIRROR_PUMPING,
    kMIRROR_FAILED,
    kMIRROR_DISCONNECTED,
    kMIRROR_CONNECTING,
    kMIRROR_AWAITING_INTERFACE,
    kMIRROR_MAX,
} TMIRROR_STATE;

/*
 * Mirrors one hub interface into a local CAN port for as long as it runs. A
 * lost hub is reconnected on the agent's backoff; an interface the hub does
 * not list (yet) is asked for again every MIRROR_INTERFACE_RETRY_MS, since
 * after a hub restart the mirror is usually back before the agent. Only a
 * refusal that waiting cannot change (read denied, reliable unsupported)
 * ends in kMIRROR_FAILED. Interface ids are hub-session scoped, so a mirror
 * opened by id fails on a lost hub instead of reopening whatever bus holds
 * that id after a restart.
 */

typedef struct {
    TransportPort *hub;
    CanPort *can;
    uint32_t interface_id;
    char interface_name[INTERFACE_NAME_NAMESPACED_SIZE];
    char name[HELLO_NAME_SIZE];
    ReconnectBackoff backoff;
    uint64_t next_attempt_at_us;
    uint32_t pending_reconnect_delay_ms;
    uint16_t list_offset;
    uint8_t channel;
    uint8_t state;
    bool can_write;
    bool pending_write;
    bool reliable;
    bool resolve_by_name;
} MirrorApp;

void MirrorApp_Init(MirrorApp *self, TransportPort *hub, CanPort *can, uint32_t interface_id, const char *interface_name);
void MirrorApp_SetName(MirrorApp *self, const char *name);
void MirrorApp_SetReliable(MirrorApp *self, bool reliable);
TransportEvents MirrorApp_TransportEvents(MirrorApp *self);
void MirrorApp_Tick(MirrorApp *self, uint64_t now_us);
void MirrorApp_OnCanFrame(MirrorApp *self, const FrameMessage *frame);
uint8_t MirrorApp_State(const MirrorApp *self);
bool MirrorApp_CanWrite(const MirrorApp *self);
uint32_t MirrorApp_PendingReconnectDelayMs(const MirrorApp *self);
