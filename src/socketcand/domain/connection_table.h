#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "socketcand/domain/ascii_framer.h"
#include "socketcand/domain/socketcand_codec.h"

/*
 * Per-connection state for the socketcand server. One entry per accepted ASCII
 * client. Each entry owns its inbound AsciiFramer and tracks the socketcand
 * mode plus the hub binding (interface_id + session channel) once a bus is
 * opened. Channel is unique per connection (one hub OPEN per connection), so a
 * delivered frame's channel resolves to exactly one connection. Freestanding.
 */

#define SOCKETCAND_CONNECTIONS_MAX 32

typedef enum tsocketcand_mode_e {
    kSOCKETCAND_MODE_NO_BUS = 0,
    kSOCKETCAND_MODE_BCM,
    kSOCKETCAND_MODE_RAW,
    kSOCKETCAND_MODE_MAX,
} TSOCKETCAND_MODE;

typedef enum tsocketcand_open_state_e {
    kSOCKETCAND_OPEN_NONE = 0,
    kSOCKETCAND_OPEN_PENDING_WRITE,
    kSOCKETCAND_OPEN_PENDING_READ,
    kSOCKETCAND_OPEN_DONE,
    kSOCKETCAND_OPEN_MAX,
} TSOCKETCAND_OPEN_STATE;

typedef struct {
    bool in_use;
    uint32_t connection_id;
    AsciiFramer framer;
    uint8_t mode;
    uint8_t open_state;
    uint32_t interface_id;
    uint8_t channel;
    bool channel_valid;
    bool can_write;
    bool reattaching;
    char bus[SOCKETCAND_BUS_NAME_SIZE];
} SocketcandConnection;

typedef struct {
    SocketcandConnection connections[SOCKETCAND_CONNECTIONS_MAX];
} ConnectionTable;

void ConnectionTable_Reset(ConnectionTable *self);
SocketcandConnection *ConnectionTable_Add(ConnectionTable *self, uint32_t connection_id);
SocketcandConnection *ConnectionTable_Find(ConnectionTable *self, uint32_t connection_id);
SocketcandConnection *ConnectionTable_FindByChannel(ConnectionTable *self, uint8_t channel);
SocketcandConnection *ConnectionTable_FindPendingOpen(ConnectionTable *self, uint32_t interface_id);
void ConnectionTable_Remove(ConnectionTable *self, uint32_t connection_id);
