#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "agent/ports/transport_events.h"
#include "agent/ports/transport_port.h"
#include "socketcand/ports/socketcand_server_events.h"
#include "socketcand/ports/socketcand_server_port.h"
#include "socketcand/socketcand_bridge.h"

/*
 * Abstract assembly of the socketcand bridge application: owns the core and
 * hands out the inbound event contracts for the platform to plug into its
 * adapters (hub transport and local socketcand server). Freestanding.
 */
typedef struct {
    SocketcandBridge bridge;
} SocketcandApp;

void SocketcandApp_Init(SocketcandApp *self, TransportPort *hub, SocketcandServerPort *server, const char *device_name, const char *beacon_url, bool beacon_enabled);
void SocketcandApp_SetName(SocketcandApp *self, const char *name);
TransportEvents SocketcandApp_TransportEvents(SocketcandApp *self);
SocketcandServerEvents SocketcandApp_ServerEvents(SocketcandApp *self);
void SocketcandApp_Tick(SocketcandApp *self, uint64_t now_us);
