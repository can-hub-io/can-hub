#define _GNU_SOURCE

#include "platform/linux/socketcand/socketcand_server.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include "platform/linux/socketcand/beacon_udp.h"

#define LISTEN_BACKLOG 8
#define RX_CHUNK_SIZE 2048

static bool portWriteClient(void *context, uint32_t connection_id, const uint8_t *data, size_t size);
static void portCloseClient(void *context, uint32_t connection_id);
static void portSendBeacon(void *context, const uint8_t *data, size_t size);
static SocketcandServerSlot *findSlotById(SocketcandServer *self, uint32_t connection_id);
static SocketcandServerSlot *findFreeSlot(SocketcandServer *self);
static bool queueSlot(SocketcandServerSlot *slot, const uint8_t *data, size_t size);
static bool flushSlot(SocketcandServerSlot *slot);
static void closeSlot(SocketcandServerSlot *slot);
static void closeSlotAndNotify(SocketcandServer *self, SocketcandServerSlot *slot);

/* ---------- public ---------- */

bool SocketcandServer_Init(
    SocketcandServer *self,
    const char *bind_address,
    const char *port,
    uint16_t beacon_port,
    const SocketcandServerEvents *events
)
{
    struct sockaddr_in address;
    int32_t reuse = 1;
    uint8_t slot;

    memset(self, 0, sizeof(*self));
    self->port.context = self;
    self->port.write_client = portWriteClient;
    self->port.close_client = portCloseClient;
    self->port.send_beacon = portSendBeacon;
    self->events = *events;
    self->next_connection_id = 1;
    self->beacon_port = beacon_port;
    for(slot=0; slot<SOCKETCAND_SERVER_SLOTS_MAX; slot++) {
        self->slots[slot].fd = SOCKETCAND_SERVER_NO_SOCKET;
    }

    self->listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (self->listen_fd < 0) {
        return false;
    }
    setsockopt(self->listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)atoi(port));
    if (inet_pton(AF_INET, bind_address, &address.sin_addr) != 1) {
        return false;
    }
    if (bind(self->listen_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        return false;
    }
    if (listen(self->listen_fd, LISTEN_BACKLOG) != 0) {
        return false;
    }

    self->beacon_fd = BeaconUdp_Open();

    return true;
}

SocketcandServerPort *SocketcandServer_Port(SocketcandServer *self)
{
    return &self->port;
}

int32_t SocketcandServer_ListenFd(const SocketcandServer *self)
{
    return self->listen_fd;
}

int32_t SocketcandServer_SlotFd(const SocketcandServer *self, uint8_t slot)
{
    return self->slots[slot].fd;
}

bool SocketcandServer_SlotWantsWritable(const SocketcandServer *self, uint8_t slot)
{
    return self->slots[slot].tx_used > 0;
}

void SocketcandServer_OnAcceptReady(SocketcandServer *self)
{
    SocketcandServerSlot *slot;
    int32_t peer_fd;
    int32_t nodelay = 1;

    for (;;) {
        peer_fd = accept4(self->listen_fd, NULL, NULL, SOCK_NONBLOCK);
        if (peer_fd < 0) {
            return;
        }

        slot = findFreeSlot(self);
        if (slot == NULL) {
            close(peer_fd);
            continue;
        }

        setsockopt(peer_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
        slot->fd = peer_fd;
        slot->connection_id = self->next_connection_id++;
        slot->tx_used = 0;
        self->events.on_client_connected(self->events.context, slot->connection_id);
    }
}

void SocketcandServer_OnSlotReadable(SocketcandServer *self, uint8_t slot_index)
{
    SocketcandServerSlot *slot = &self->slots[slot_index];
    uint8_t chunk[RX_CHUNK_SIZE];
    uint32_t connection_id = slot->connection_id;
    int32_t fd = slot->fd;
    ssize_t received;

    if (fd == SOCKETCAND_SERVER_NO_SOCKET) {
        return;
    }

    for (;;) {
        received = recv(fd, chunk, sizeof(chunk), 0);
        if (received > 0) {
            self->events.on_client_bytes(self->events.context, connection_id, chunk, (size_t)received);
            if (slot->fd != fd) {
                return;
            }
            continue;
        }
        if (received == 0) {
            closeSlotAndNotify(self, slot);
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        closeSlotAndNotify(self, slot);
        return;
    }
}

void SocketcandServer_OnSlotWritable(SocketcandServer *self, uint8_t slot_index)
{
    SocketcandServerSlot *slot = &self->slots[slot_index];

    if (slot->fd == SOCKETCAND_SERVER_NO_SOCKET) {
        return;
    }
    if (!flushSlot(slot)) {
        closeSlotAndNotify(self, slot);
    }
}

void SocketcandServer_Close(SocketcandServer *self)
{
    uint8_t slot;

    for(slot=0; slot<SOCKETCAND_SERVER_SLOTS_MAX; slot++) {
        closeSlot(&self->slots[slot]);
    }
    if (self->listen_fd >= 0) {
        close(self->listen_fd);
        self->listen_fd = SOCKETCAND_SERVER_NO_SOCKET;
    }
    BeaconUdp_Close(self->beacon_fd);
    self->beacon_fd = SOCKETCAND_SERVER_NO_SOCKET;
}

/* ---------- private: socketcand server port ---------- */

static bool portWriteClient(void *context, uint32_t connection_id, const uint8_t *data, size_t size)
{
    SocketcandServer *self = context;
    SocketcandServerSlot *slot = findSlotById(self, connection_id);

    if (slot == NULL) {
        return false;
    }
    if (!queueSlot(slot, data, size)) {
        return false;
    }
    flushSlot(slot);

    return true;
}

static void portCloseClient(void *context, uint32_t connection_id)
{
    SocketcandServer *self = context;
    SocketcandServerSlot *slot = findSlotById(self, connection_id);

    if (slot != NULL) {
        closeSlot(slot);
    }
}

static void portSendBeacon(void *context, const uint8_t *data, size_t size)
{
    SocketcandServer *self = context;

    BeaconUdp_Send(self->beacon_fd, self->beacon_port, data, size);
}

/* ---------- private ---------- */

static SocketcandServerSlot *findSlotById(SocketcandServer *self, uint32_t connection_id)
{
    uint8_t slot;

    for(slot=0; slot<SOCKETCAND_SERVER_SLOTS_MAX; slot++) {
        if (self->slots[slot].fd != SOCKETCAND_SERVER_NO_SOCKET && self->slots[slot].connection_id == connection_id) {
            return &self->slots[slot];
        }
    }

    return NULL;
}

static SocketcandServerSlot *findFreeSlot(SocketcandServer *self)
{
    uint8_t slot;

    for(slot=0; slot<SOCKETCAND_SERVER_SLOTS_MAX; slot++) {
        if (self->slots[slot].fd == SOCKETCAND_SERVER_NO_SOCKET) {
            return &self->slots[slot];
        }
    }

    return NULL;
}

static bool queueSlot(SocketcandServerSlot *slot, const uint8_t *data, size_t size)
{
    if (slot->tx_used + size > SOCKETCAND_SERVER_TX_BACKLOG_SIZE) {
        return false;
    }
    memcpy(slot->tx_backlog + slot->tx_used, data, size);
    slot->tx_used += size;

    return true;
}

static bool flushSlot(SocketcandServerSlot *slot)
{
    ssize_t sent;

    while (slot->tx_used > 0) {
        sent = send(slot->fd, slot->tx_backlog, slot->tx_used, MSG_NOSIGNAL);
        if (sent > 0) {
            memmove(slot->tx_backlog, slot->tx_backlog + sent, slot->tx_used - (size_t)sent);
            slot->tx_used -= (size_t)sent;
            continue;
        }
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return true;
        }
        return false;
    }

    return true;
}

static void closeSlot(SocketcandServerSlot *slot)
{
    if (slot->fd != SOCKETCAND_SERVER_NO_SOCKET) {
        close(slot->fd);
        slot->fd = SOCKETCAND_SERVER_NO_SOCKET;
        slot->tx_used = 0;
    }
}

static void closeSlotAndNotify(SocketcandServer *self, SocketcandServerSlot *slot)
{
    uint32_t connection_id = slot->connection_id;

    closeSlot(slot);
    self->events.on_client_disconnected(self->events.context, connection_id);
}
