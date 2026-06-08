#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "socketcand/ports/socketcand_server_events.h"
#include "socketcand/ports/socketcand_server_port.h"

/*
 * Local socketcand TCP server adapter. Accepts up to SOCKETCAND_SERVER_SLOTS_MAX
 * ASCII clients, hands their raw bytes to SocketcandServerEvents and writes the
 * bridge's responses back (per-slot TX backlog). Also owns the discovery beacon
 * UDP socket, so it implements the whole SocketcandServerPort. The platform loop
 * polls ListenFd plus every bound SlotFd, watching EPOLLOUT for slots with
 * pending TX. close_client closes the socket WITHOUT firing on_client_disconnected
 * (the bridge initiated it); peer-initiated EOF on the read path does fire it.
 */

#define SOCKETCAND_SERVER_SLOTS_MAX 32
#define SOCKETCAND_SERVER_TX_BACKLOG_SIZE 8192
#define SOCKETCAND_SERVER_NO_SOCKET (-1)

typedef struct {
    int32_t fd;
    uint32_t connection_id;
    uint8_t tx_backlog[SOCKETCAND_SERVER_TX_BACKLOG_SIZE];
    size_t tx_used;
} SocketcandServerSlot;

typedef struct {
    SocketcandServerPort port;
    SocketcandServerEvents events;
    int32_t listen_fd;
    int32_t beacon_fd;
    uint16_t beacon_port;
    uint32_t next_connection_id;
    SocketcandServerSlot slots[SOCKETCAND_SERVER_SLOTS_MAX];
} SocketcandServer;

bool SocketcandServer_Init(
    SocketcandServer *self,
    const char *bind_address,
    const char *port,
    uint16_t beacon_port,
    const SocketcandServerEvents *events
);
SocketcandServerPort *SocketcandServer_Port(SocketcandServer *self);
int32_t SocketcandServer_ListenFd(const SocketcandServer *self);
int32_t SocketcandServer_SlotFd(const SocketcandServer *self, uint8_t slot);
bool SocketcandServer_SlotWantsWritable(const SocketcandServer *self, uint8_t slot);
void SocketcandServer_OnAcceptReady(SocketcandServer *self);
void SocketcandServer_OnSlotReadable(SocketcandServer *self, uint8_t slot);
void SocketcandServer_OnSlotWritable(SocketcandServer *self, uint8_t slot);
void SocketcandServer_Close(SocketcandServer *self);
