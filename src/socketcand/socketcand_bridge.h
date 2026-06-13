#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "agent/ports/transport_events.h"
#include "agent/ports/transport_port.h"
#include "protocol/hello_message.h"
#include "socketcand/domain/connection_table.h"
#include "socketcand/domain/interface_catalogue.h"
#include "socketcand/ports/socketcand_server_events.h"
#include "socketcand/ports/socketcand_server_port.h"

/*
 * Bridge core: a hub client on one side, a socketcand server on the other.
 * Freestanding state machine — the platform loop pushes events in (hub
 * TransportEvents and SocketcandServerEvents) and injects time (Tick); the two
 * ports carry everything that leaves the core.
 *
 * Hub side: connect -> HELLO(client) -> LIST (paginated, refreshed) builds the
 * interface catalogue; per socketcand connection an OPEN maps a namespaced bus
 * to a session channel. socketcand side: rawmode only; frames delivered by the
 * hub on a channel fan out to the one connection that owns it.
 */

#define SOCKETCAND_DEVICE_NAME_SIZE 64
#define SOCKETCAND_URL_SIZE 288

typedef enum tsocketcand_hub_state_e {
    kSOCKETCAND_HUB_DISCONNECTED = 0,
    kSOCKETCAND_HUB_CONNECTING,
    kSOCKETCAND_HUB_READY,
    kSOCKETCAND_HUB_MAX,
} TSOCKETCAND_HUB_STATE;

typedef struct {
    TransportPort *hub;
    SocketcandServerPort *server;
    InterfaceCatalogue catalogue;
    ConnectionTable connections;
    uint8_t hub_state;
    uint64_t next_connect_at_us;
    uint64_t next_beacon_at_us;
    uint64_t next_list_at_us;
    uint16_t listing_offset;
    bool listing_active;
    bool beacon_enabled;
    char device_name[SOCKETCAND_DEVICE_NAME_SIZE];
    char beacon_url[SOCKETCAND_URL_SIZE];
    char name[HELLO_NAME_SIZE];
} SocketcandBridge;

void SocketcandBridge_Init(SocketcandBridge *self, TransportPort *hub, SocketcandServerPort *server, const char *device_name, const char *beacon_url, bool beacon_enabled);
void SocketcandBridge_SetName(SocketcandBridge *self, const char *name);
TransportEvents SocketcandBridge_TransportEvents(SocketcandBridge *self);
SocketcandServerEvents SocketcandBridge_ServerEvents(SocketcandBridge *self);
void SocketcandBridge_Tick(SocketcandBridge *self, uint64_t now_us);
uint8_t SocketcandBridge_HubState(const SocketcandBridge *self);
