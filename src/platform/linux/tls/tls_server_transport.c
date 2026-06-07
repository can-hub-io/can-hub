#define _GNU_SOURCE

#include "platform/linux/tls/tls_server_transport.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include "platform/linux/clock/clock.h"
#include "platform/linux/shared/tls_identity.h"
#include "protocol/message_header.h"

#define LISTEN_BACKLOG 8

static bool portSendControl(void *context, uint32_t peer_id, const uint8_t *data, size_t size);
static bool portSendFrame(void *context, uint32_t peer_id, const uint8_t *data, size_t size);
static void portClosePeer(void *context, uint32_t peer_id);
static TlsServerPeer *findPeer(TlsServerTransport *self, uint32_t peer_id);
static TlsServerPeer *findFreeSlot(TlsServerTransport *self);
static void pumpHandshake(TlsServerTransport *self, TlsServerPeer *peer);
static void announcePeer(TlsServerTransport *self, TlsServerPeer *peer);
static void dispatchMessages(TlsServerTransport *self, TlsServerPeer *peer);
static void closePeer(TlsServerTransport *self, TlsServerPeer *peer, bool notify);

/* ---------- public ---------- */

bool TlsServerTransport_Init(
    TlsServerTransport *self,
    const char *port,
    const char *certificate_file,
    const char *key_file,
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
    for(slot=0; slot<TLS_SERVER_PEERS_MAX; slot++) {
        TlsChannel_Reset(&self->peers[slot].channel);
    }

    if (!TlsServerSecurity_Init(&self->security, certificate_file, key_file)) {
        return false;
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

HubTransportPort *TlsServerTransport_Port(TlsServerTransport *self)
{
    return &self->port;
}

int32_t TlsServerTransport_ListenFd(const TlsServerTransport *self)
{
    return self->listen_fd;
}

int32_t TlsServerTransport_SlotFd(const TlsServerTransport *self, uint8_t slot)
{
    return self->peers[slot].channel.fd;
}

bool TlsServerTransport_SlotWantsWritable(const TlsServerTransport *self, uint8_t slot)
{
    if (!TlsChannel_IsBound(&self->peers[slot].channel)) {
        return false;
    }

    return TlsChannel_WantsWrite(&self->peers[slot].channel);
}

void TlsServerTransport_OnAcceptReady(TlsServerTransport *self)
{
    TlsServerPeer *peer;
    gnutls_session_t session;
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
        if (!TlsServerSecurity_NewSession(&self->security, &session)) {
            close(peer_fd);
            continue;
        }

        setsockopt(peer_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
        TlsChannel_Bind(&peer->channel, peer_fd, session);
        peer->peer_id = self->next_peer_id++;
        peer->announced = false;
        pumpHandshake(self, peer);
    }
}

void TlsServerTransport_OnSlotReadable(TlsServerTransport *self, uint8_t slot)
{
    TlsServerPeer *peer = &self->peers[slot];

    if (!TlsChannel_IsBound(&peer->channel)) {
        return;
    }

    if (!TlsChannel_IsEstablished(&peer->channel)) {
        pumpHandshake(self, peer);
        return;
    }

    if (!TlsChannel_Receive(&peer->channel)) {
        dispatchMessages(self, peer);
        closePeer(self, peer, true);
        return;
    }

    dispatchMessages(self, peer);
}

void TlsServerTransport_OnSlotWritable(TlsServerTransport *self, uint8_t slot)
{
    TlsServerPeer *peer = &self->peers[slot];

    if (!TlsChannel_IsBound(&peer->channel)) {
        return;
    }

    if (!TlsChannel_IsEstablished(&peer->channel)) {
        pumpHandshake(self, peer);
        return;
    }

    if (!TlsChannel_Flush(&peer->channel)) {
        closePeer(self, peer, true);
    }
}

/* ---------- private: hub transport port ---------- */

static bool portSendControl(void *context, uint32_t peer_id, const uint8_t *data, size_t size)
{
    TlsServerTransport *self = context;
    TlsServerPeer *peer = findPeer(self, peer_id);

    if (peer == NULL || !peer->announced) {
        return false;
    }
    if (!TlsChannel_Queue(&peer->channel, data, size)) {
        return false;
    }
    if (!TlsChannel_Flush(&peer->channel)) {
        closePeer(self, peer, true);
        return false;
    }

    return true;
}

static bool portSendFrame(void *context, uint32_t peer_id, const uint8_t *data, size_t size)
{
    TlsServerTransport *self = context;
    TlsServerPeer *peer = findPeer(self, peer_id);

    if (peer == NULL) {
        return false;
    }
    if (TlsChannel_FreeTxSpace(&peer->channel) < size) {
        return false;
    }

    return portSendControl(context, peer_id, data, size);
}

static void portClosePeer(void *context, uint32_t peer_id)
{
    TlsServerTransport *self = context;
    TlsServerPeer *peer = findPeer(self, peer_id);

    if (peer != NULL) {
        closePeer(self, peer, false);
    }
}

/* ---------- private ---------- */

static TlsServerPeer *findPeer(TlsServerTransport *self, uint32_t peer_id)
{
    uint8_t slot;

    for(slot=0; slot<TLS_SERVER_PEERS_MAX; slot++) {
        if (TlsChannel_IsBound(&self->peers[slot].channel) && self->peers[slot].peer_id == peer_id) {
            return &self->peers[slot];
        }
    }

    return NULL;
}

static TlsServerPeer *findFreeSlot(TlsServerTransport *self)
{
    uint8_t slot;

    for(slot=0; slot<TLS_SERVER_PEERS_MAX; slot++) {
        if (!TlsChannel_IsBound(&self->peers[slot].channel)) {
            return &self->peers[slot];
        }
    }

    return NULL;
}

static void pumpHandshake(TlsServerTransport *self, TlsServerPeer *peer)
{
    if (!TlsChannel_Pump(&peer->channel)) {
        closePeer(self, peer, false);
        return;
    }

    if (TlsChannel_IsEstablished(&peer->channel) && !peer->announced) {
        announcePeer(self, peer);
    }
}

static void announcePeer(TlsServerTransport *self, TlsServerPeer *peer)
{
    char fingerprint_hex[TLS_IDENTITY_FINGERPRINT_HEX_SIZE];
    bool has_fingerprint = TlsChannel_PeerFingerprint(&peer->channel, fingerprint_hex);

    peer->announced = true;
    self->events.on_peer_connected(
        self->events.context,
        peer->peer_id,
        has_fingerprint ? fingerprint_hex : NULL,
        false
    );

    if (!TlsChannel_Receive(&peer->channel)) {
        closePeer(self, peer, true);
        return;
    }
    dispatchMessages(self, peer);
}

static void dispatchMessages(TlsServerTransport *self, TlsServerPeer *peer)
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

static void closePeer(TlsServerTransport *self, TlsServerPeer *peer, bool notify)
{
    uint32_t peer_id = peer->peer_id;
    bool was_announced = peer->announced;
    int32_t peer_fd = peer->channel.fd;

    TlsChannel_Close(&peer->channel);
    close(peer_fd);
    peer->announced = false;

    if (notify && was_announced) {
        self->events.on_peer_disconnected(self->events.context, peer_id, Clock_RealtimeUs());
    }
}
