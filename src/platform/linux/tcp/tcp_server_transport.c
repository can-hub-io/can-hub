#define _GNU_SOURCE

#include "platform/linux/tcp/tcp_server_transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include "platform/linux/clock/clock.h"
#include "protocol/message_header.h"

#define LISTEN_BACKLOG 8

static bool portSendControl(void *context, uint32_t peer_id, const uint8_t *data, size_t size);
static bool portSendFrame(void *context, uint32_t peer_id, const uint8_t *data, size_t size);
static void portClosePeer(void *context, uint32_t peer_id);
static TcpServerPeer *findPeer(TcpServerTransport *self, uint32_t peer_id);
static TcpServerPeer *findFreeSlot(TcpServerTransport *self);
static void dispatchMessages(TcpServerTransport *self, TcpServerPeer *peer);
static void closePeer(TcpServerTransport *self, TcpServerPeer *peer, bool notify);

/* ---------- public ---------- */

bool TcpServerTransport_Init(
    TcpServerTransport *self,
    const char *port,
    uint32_t peer_id_base,
    const HubTransportEvents *events
)
{
    struct sockaddr_in address;
    int32_t reuse = 1;
    uint8_t slot;

    memset(self, 0, sizeof(*self));
    self->port.context = self;
    self->port.send_control = portSendControl;
    self->port.send_frame = portSendFrame;
    self->port.close_peer = portClosePeer;
    self->events = *events;
    self->next_peer_id = peer_id_base;
    for(slot=0; slot<TCP_SERVER_PEERS_MAX; slot++) {
        TcpChannel_Unbind(&self->peers[slot].channel);
    }

    self->listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (self->listen_fd < 0) {
        return false;
    }
    setsockopt(self->listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((uint16_t)atoi(port));
    if (bind(self->listen_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        return false;
    }

    return listen(self->listen_fd, LISTEN_BACKLOG) == 0;
}

HubTransportPort *TcpServerTransport_Port(TcpServerTransport *self)
{
    return &self->port;
}

int32_t TcpServerTransport_ListenFd(const TcpServerTransport *self)
{
    return self->listen_fd;
}

int32_t TcpServerTransport_SlotFd(const TcpServerTransport *self, uint8_t slot)
{
    return self->peers[slot].channel.fd;
}

bool TcpServerTransport_SlotWantsWritable(const TcpServerTransport *self, uint8_t slot)
{
    return TcpChannel_HasPendingTx(&self->peers[slot].channel);
}

void TcpServerTransport_OnAcceptReady(TcpServerTransport *self)
{
    TcpServerPeer *peer;
    int32_t peer_fd;
    int32_t nodelay = 1;

    for (;;) {
        peer_fd = accept4(self->listen_fd, NULL, NULL, SOCK_NONBLOCK);
        if (peer_fd < 0) {
            return;
        }

        peer = findFreeSlot(self);
        if (peer == NULL) {
            close(peer_fd);
            continue;
        }

        setsockopt(peer_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
        TcpChannel_Bind(&peer->channel, peer_fd);
        peer->peer_id = self->next_peer_id++;
        self->events.on_peer_connected(self->events.context, peer->peer_id);
    }
}

void TcpServerTransport_OnSlotReadable(TcpServerTransport *self, uint8_t slot)
{
    TcpServerPeer *peer = &self->peers[slot];

    if (!TcpChannel_IsBound(&peer->channel)) {
        return;
    }

    if (!TcpChannel_Receive(&peer->channel)) {
        closePeer(self, peer, true);
        return;
    }

    dispatchMessages(self, peer);
}

void TcpServerTransport_OnSlotWritable(TcpServerTransport *self, uint8_t slot)
{
    TcpServerPeer *peer = &self->peers[slot];

    if (!TcpChannel_IsBound(&peer->channel)) {
        return;
    }

    if (!TcpChannel_Flush(&peer->channel)) {
        closePeer(self, peer, true);
    }
}

/* ---------- private: hub transport port ---------- */

static bool portSendControl(void *context, uint32_t peer_id, const uint8_t *data, size_t size)
{
    TcpServerTransport *self = context;
    TcpServerPeer *peer = findPeer(self, peer_id);

    if (peer == NULL) {
        return false;
    }
    if (!TcpChannel_Queue(&peer->channel, data, size)) {
        return false;
    }
    if (!TcpChannel_Flush(&peer->channel)) {
        closePeer(self, peer, true);
        return false;
    }

    return true;
}

static bool portSendFrame(void *context, uint32_t peer_id, const uint8_t *data, size_t size)
{
    TcpServerTransport *self = context;
    TcpServerPeer *peer = findPeer(self, peer_id);

    if (peer == NULL) {
        return false;
    }
    if (TcpChannel_FreeTxSpace(&peer->channel) < size) {
        return false;
    }

    return portSendControl(context, peer_id, data, size);
}

static void portClosePeer(void *context, uint32_t peer_id)
{
    TcpServerTransport *self = context;
    TcpServerPeer *peer = findPeer(self, peer_id);

    if (peer != NULL) {
        closePeer(self, peer, false);
    }
}

/* ---------- private ---------- */

static TcpServerPeer *findPeer(TcpServerTransport *self, uint32_t peer_id)
{
    uint8_t slot;

    for(slot=0; slot<TCP_SERVER_PEERS_MAX; slot++) {
        if (TcpChannel_IsBound(&self->peers[slot].channel) && self->peers[slot].peer_id == peer_id) {
            return &self->peers[slot];
        }
    }

    return NULL;
}

static TcpServerPeer *findFreeSlot(TcpServerTransport *self)
{
    uint8_t slot;

    for(slot=0; slot<TCP_SERVER_PEERS_MAX; slot++) {
        if (!TcpChannel_IsBound(&self->peers[slot].channel)) {
            return &self->peers[slot];
        }
    }

    return NULL;
}

static void dispatchMessages(TcpServerTransport *self, TcpServerPeer *peer)
{
    MessageHeader header;
    const uint8_t *message;
    size_t message_size;

    for (;;) {
        message_size = MessageFramer_NextMessage(&peer->channel.framer, &message);
        if (message_size == 0) {
            return;
        }

        MessageHeader_Decode(&header, message, message_size);
        if (header.type == kMESSAGE_TYPE_FRAME) {
            self->events.on_peer_frame(self->events.context, peer->peer_id, message, message_size);
        } else {
            self->events.on_peer_control(
                self->events.context,
                peer->peer_id,
                message,
                message_size,
                Clock_RealtimeUs()
            );
        }
        MessageFramer_Consume(&peer->channel.framer, message_size);
    }
}

static void closePeer(TcpServerTransport *self, TcpServerPeer *peer, bool notify)
{
    uint32_t peer_id = peer->peer_id;

    close(peer->channel.fd);
    TcpChannel_Unbind(&peer->channel);

    if (notify) {
        self->events.on_peer_disconnected(self->events.context, peer_id, Clock_RealtimeUs());
    }
}
