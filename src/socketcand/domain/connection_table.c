#include "socketcand/domain/connection_table.h"

#include <string.h>

/* ---------- public ---------- */

void ConnectionTable_Reset(ConnectionTable *self)
{
    memset(self, 0, sizeof(*self));
}

SocketcandConnection *ConnectionTable_Add(ConnectionTable *self, uint32_t connection_id)
{
    uint8_t i;

    for(i=0; i<SOCKETCAND_CONNECTIONS_MAX; i++) {
        if (self->connections[i].in_use) {
            continue;
        }
        memset(&self->connections[i], 0, sizeof(self->connections[i]));
        self->connections[i].in_use = true;
        self->connections[i].connection_id = connection_id;
        self->connections[i].mode = kSOCKETCAND_MODE_NO_BUS;
        self->connections[i].open_state = kSOCKETCAND_OPEN_NONE;
        AsciiFramer_Reset(&self->connections[i].framer);
        return &self->connections[i];
    }

    return NULL;
}

SocketcandConnection *ConnectionTable_Find(ConnectionTable *self, uint32_t connection_id)
{
    uint8_t i;

    for(i=0; i<SOCKETCAND_CONNECTIONS_MAX; i++) {
        if (self->connections[i].in_use && self->connections[i].connection_id == connection_id) {
            return &self->connections[i];
        }
    }

    return NULL;
}

SocketcandConnection *ConnectionTable_FindByChannel(ConnectionTable *self, uint8_t channel)
{
    uint8_t i;

    for(i=0; i<SOCKETCAND_CONNECTIONS_MAX; i++) {
        if (self->connections[i].in_use && self->connections[i].channel_valid && self->connections[i].channel == channel) {
            return &self->connections[i];
        }
    }

    return NULL;
}

SocketcandConnection *ConnectionTable_FindPendingOpen(ConnectionTable *self, uint32_t interface_id)
{
    SocketcandConnection *oldest = NULL;
    uint8_t i;

    for(i=0; i<SOCKETCAND_CONNECTIONS_MAX; i++) {
        if (!self->connections[i].in_use || self->connections[i].interface_id != interface_id) {
            continue;
        }
        if (self->connections[i].open_state != kSOCKETCAND_OPEN_PENDING_WRITE
            && self->connections[i].open_state != kSOCKETCAND_OPEN_PENDING_READ) {
            continue;
        }
        if (oldest == NULL || self->connections[i].open_seq < oldest->open_seq) {
            oldest = &self->connections[i];
        }
    }

    return oldest;
}

uint32_t ConnectionTable_NextOpenSeq(ConnectionTable *self)
{
    self->next_open_seq++;
    return self->next_open_seq;
}

void ConnectionTable_Remove(ConnectionTable *self, uint32_t connection_id)
{
    SocketcandConnection *connection = ConnectionTable_Find(self, connection_id);

    if (connection != NULL) {
        connection->in_use = false;
    }
}
