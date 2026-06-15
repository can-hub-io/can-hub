#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "agent/ports/transport_events.h"
#include "agent/ports/transport_port.h"
#include "shared/egress_shaper.h"
#include "protocol/error_message.h"
#include "protocol/frame_message.h"
#include "protocol/hello_message.h"
#include "protocol/interface_name.h"
#include "protocol/list_message.h"
#include "protocol/subscribe_message.h"

#define CLIENT_ERROR_MALFORMED_REPLY 0xFFFE
#define CLIENT_ERROR_INTERFACE_NOT_FOUND 0xFFFF

typedef enum tclient_state_e {
    kCLIENT_DISCONNECTED = 0,
    kCLIENT_READY,
    kCLIENT_LISTING,
    kCLIENT_RESOLVING,
    kCLIENT_OPENING,
    kCLIENT_OPEN,
    kCLIENT_STATE_MAX,
} TCLIENT_STATE;

typedef struct {
    void *context;
    void (*on_list_reply)(void *context, const ListReplyMessage *reply);
    void (*on_open_result)(void *context, uint8_t status, uint8_t channel, uint32_t interface_id);
    void (*on_frame)(void *context, const FrameMessage *frame);
    void (*on_error)(void *context, uint16_t code, const char *detail);
    void (*on_disconnected)(void *context);
} ClientEvents;

typedef struct {
    TransportPort *hub;
    ClientEvents events;
    uint8_t state;
    uint8_t pending;
    bool connected;
    uint16_t list_offset;
    char interface_name[INTERFACE_NAME_NAMESPACED_SIZE];
    uint32_t interface_id;
    uint8_t open_flags;
    uint8_t channel;
    uint8_t filter_count;
    CanFilter filters[SUBSCRIBE_FILTERS_MAX];
    char name[HELLO_NAME_SIZE];
    EgressShaper shaper;
    uint64_t frames_paced_dropped;
} Client;

void Client_Init(Client *self, TransportPort *hub, const ClientEvents *events);
void Client_SetName(Client *self, const char *name);
TransportEvents Client_TransportEvents(Client *self);
void Client_RequestList(Client *self, uint16_t offset);
void Client_OpenById(Client *self, uint32_t interface_id, uint8_t flags);
void Client_OpenByName(Client *self, const char *interface_name, uint8_t flags);
void Client_SetFilters(Client *self, const CanFilter *filters, uint8_t count);
bool Client_SendFrame(Client *self, FrameMessage *frame, uint64_t now_us);
uint8_t Client_State(const Client *self);
uint8_t Client_Channel(const Client *self);
