#include "platform/linux/quic/quic_server_transport.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/timerfd.h>

#include "platform/linux/clock/clock.h"
#include "platform/linux/quic/quic_egress.h"
#include "protocol/message_header.h"

#define UDP_PACKET_BUFFER_SIZE 1452

static bool portSendControl(void *context, uint32_t peer_id, const uint8_t *data, size_t size);
static bool portSendFrame(void *context, uint32_t peer_id, const uint8_t *data, size_t size);
static void portClosePeer(void *context, uint32_t peer_id);
static QuicServerPeer *findPeerById(QuicServerTransport *self, uint32_t peer_id);
static QuicServerPeer *findPeerByDcid(QuicServerTransport *self, const uint8_t *packet, size_t packet_size);
static void refreshPeerAddress(
    QuicServerPeer *peer,
    const struct sockaddr_storage *address,
    socklen_t address_length
);
static QuicServerPeer *acceptPeer(
    QuicServerTransport *self,
    const struct sockaddr_storage *address,
    socklen_t address_length,
    const uint8_t *packet,
    size_t packet_size
);
static void makePeerPath(QuicServerTransport *self, QuicServerPeer *peer, ngtcp2_path *path);
static bool flushPeer(QuicServerTransport *self, QuicServerPeer *peer, const uint8_t *datagram, size_t datagram_size);
static void sendPacketToPeer(void *context, const uint8_t *packet, size_t size);
static void sendToPeer(QuicServerTransport *self, QuicServerPeer *peer, const uint8_t *packet, size_t size);
static void rearmTimer(QuicServerTransport *self);
static void teardownPeer(QuicServerTransport *self, QuicServerPeer *peer, bool notify);
static void dispatchControlMessages(QuicServerTransport *self, QuicServerPeer *peer);
static void capturePeerFingerprint(QuicServerPeer *peer);
static void onHandshakeCompleted(void *context);
static void onDatagram(void *context, const uint8_t *data, size_t size);
static void onStreamData(void *context, int64_t stream_id, const uint8_t *data, size_t size);
static void onStreamAcked(void *context, int64_t stream_id, uint64_t acked_end_offset);

/* ---------- public ---------- */

bool QuicServerTransport_Init(
    QuicServerTransport *self,
    const char *bind_address,
    const char *port,
    const char *certificate_file,
    const char *key_file,
    uint32_t peer_id_base,
    const HubTransportEvents *events
)
{
    struct sockaddr_in address;

    memset(self, 0, sizeof(*self));
    self->port.context = self;
    self->port.send_control = portSendControl;
    self->port.send_frame = portSendFrame;
    self->port.close_peer = portClosePeer;
    self->events = *events;
    self->next_peer_id = peer_id_base;

    if (!QuicServerSecurity_Init(&self->security, certificate_file, key_file)) {
        return false;
    }

    self->udp_fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    self->timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (self->udp_fd < 0 || self->timer_fd < 0) {
        return false;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)atoi(port));
    if (inet_pton(AF_INET, bind_address, &address.sin_addr) != 1) {
        return false;
    }
    if (bind(self->udp_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        return false;
    }

    self->local_address_length = sizeof(self->local_address);
    getsockname(self->udp_fd, (struct sockaddr *)&self->local_address, &self->local_address_length);

    return true;
}

HubTransportPort *QuicServerTransport_Port(QuicServerTransport *self)
{
    return &self->port;
}

int32_t QuicServerTransport_UdpFd(const QuicServerTransport *self)
{
    return self->udp_fd;
}

int32_t QuicServerTransport_TimerFd(const QuicServerTransport *self)
{
    return self->timer_fd;
}

void QuicServerTransport_OnUdpReadable(QuicServerTransport *self)
{
    uint8_t packet[UDP_PACKET_BUFFER_SIZE];
    struct sockaddr_storage peer_address;
    socklen_t peer_address_length;
    QuicServerPeer *peer;
    ngtcp2_path path;
    ssize_t bytes_received;

    for (;;) {
        peer_address_length = sizeof(peer_address);
        bytes_received = recvfrom(
            self->udp_fd,
            packet,
            sizeof(packet),
            0,
            (struct sockaddr *)&peer_address,
            &peer_address_length
        );
        if (bytes_received <= 0) {
            break;
        }

        peer = findPeerByDcid(self, packet, (size_t)bytes_received);
        if (peer == NULL) {
            peer = acceptPeer(self, &peer_address, peer_address_length, packet, (size_t)bytes_received);
            if (peer == NULL) {
                continue;
            }
        } else {
            refreshPeerAddress(peer, &peer_address, peer_address_length);
        }

        makePeerPath(self, peer, &path);
        self->dispatching = true;
        if (!QuicConnection_ReadPacket(&peer->connection, &path, packet, (size_t)bytes_received)) {
            self->dispatching = false;
            teardownPeer(self, peer, true);
            continue;
        }
        self->dispatching = false;

        if (peer->close_pending) {
            flushPeer(self, peer, NULL, 0);
            teardownPeer(self, peer, false);
            continue;
        }
        flushPeer(self, peer, NULL, 0);
    }

    rearmTimer(self);
}

void QuicServerTransport_OnTimer(QuicServerTransport *self)
{
    uint64_t expiration_count;
    uint64_t now_ns;
    uint8_t i;
    ssize_t bytes_read;

    bytes_read = read(self->timer_fd, &expiration_count, sizeof(expiration_count));
    (void)bytes_read;

    now_ns = Clock_MonotonicNs();
    for(i=0; i<QUIC_SERVER_PEERS_MAX; i++) {
        QuicServerPeer *peer = &self->peers[i];

        if (!QuicConnection_IsOpen(&peer->connection)) {
            continue;
        }
        if (QuicConnection_NextExpiryNs(&peer->connection) > now_ns) {
            continue;
        }
        self->dispatching = true;
        if (!QuicConnection_HandleExpiry(&peer->connection)) {
            self->dispatching = false;
            teardownPeer(self, peer, true);
            continue;
        }
        self->dispatching = false;

        if (peer->close_pending) {
            flushPeer(self, peer, NULL, 0);
            teardownPeer(self, peer, false);
            continue;
        }
        flushPeer(self, peer, NULL, 0);
    }

    rearmTimer(self);
}

/* ---------- private: hub transport port ---------- */

static bool portSendControl(void *context, uint32_t peer_id, const uint8_t *data, size_t size)
{
    QuicServerTransport *self = context;
    QuicServerPeer *peer = findPeerById(self, peer_id);

    if (peer == NULL || !peer->connected || peer->close_pending) {
        return false;
    }
    if (!QuicControlChannel_QueueTx(&peer->control, data, size)) {
        return false;
    }
    if (self->dispatching) {
        return true;
    }

    return flushPeer(self, peer, NULL, 0);
}

static bool portSendFrame(void *context, uint32_t peer_id, const uint8_t *data, size_t size)
{
    QuicServerTransport *self = context;
    QuicServerPeer *peer = findPeerById(self, peer_id);

    if (peer == NULL || !peer->connected || peer->close_pending) {
        return false;
    }

    return flushPeer(self, peer, data, size);
}

static void portClosePeer(void *context, uint32_t peer_id)
{
    QuicServerTransport *self = context;
    QuicServerPeer *peer = findPeerById(self, peer_id);

    if (peer == NULL) {
        return;
    }
    if (self->dispatching) {
        peer->close_pending = true;
        return;
    }

    teardownPeer(self, peer, false);
}

/* ---------- private: peers ---------- */

static QuicServerPeer *findPeerById(QuicServerTransport *self, uint32_t peer_id)
{
    uint8_t i;

    for(i=0; i<QUIC_SERVER_PEERS_MAX; i++) {
        if (QuicConnection_IsOpen(&self->peers[i].connection) && self->peers[i].peer_id == peer_id) {
            return &self->peers[i];
        }
    }

    return NULL;
}

static QuicServerPeer *findPeerByDcid(QuicServerTransport *self, const uint8_t *packet, size_t packet_size)
{
    ngtcp2_version_cid version_cid;
    ngtcp2_cid dcid;
    QuicServerPeer *peer;
    uint8_t i;

    if (ngtcp2_pkt_decode_version_cid(&version_cid, packet, packet_size, QUIC_CONNECTION_CID_LENGTH) != 0) {
        return NULL;
    }
    if (version_cid.dcidlen > NGTCP2_MAX_CIDLEN) {
        return NULL;
    }
    ngtcp2_cid_init(&dcid, version_cid.dcid, version_cid.dcidlen);

    for(i=0; i<QUIC_SERVER_PEERS_MAX; i++) {
        peer = &self->peers[i];
        if (!QuicConnection_IsOpen(&peer->connection)) {
            continue;
        }
        if (ngtcp2_cid_eq(&peer->original_dcid, &dcid)) {
            return peer;
        }
        if (QuicConnection_HasLocalCid(&peer->connection, &dcid)) {
            return peer;
        }
    }

    return NULL;
}

static void refreshPeerAddress(
    QuicServerPeer *peer,
    const struct sockaddr_storage *address,
    socklen_t address_length
)
{
    memcpy(&peer->remote_address, address, address_length);
    peer->remote_address_length = address_length;
}

static QuicServerPeer *acceptPeer(
    QuicServerTransport *self,
    const struct sockaddr_storage *address,
    socklen_t address_length,
    const uint8_t *packet,
    size_t packet_size
)
{
    QuicConnectionEvents connection_events = {
        .on_handshake_completed = onHandshakeCompleted,
        .on_datagram = onDatagram,
        .on_stream_data = onStreamData,
        .on_stream_acked = onStreamAcked,
    };
    ngtcp2_pkt_hd initial_header;
    QuicServerPeer *peer = NULL;
    ngtcp2_path path;
    uint8_t i;

    if (ngtcp2_accept(&initial_header, packet, packet_size) != 0) {
        return NULL;
    }

    for(i=0; i<QUIC_SERVER_PEERS_MAX; i++) {
        if (!QuicConnection_IsOpen(&self->peers[i].connection)) {
            peer = &self->peers[i];
            break;
        }
    }
    if (peer == NULL) {
        return NULL;
    }

    memset(peer, 0, sizeof(*peer));
    peer->transport = self;
    connection_events.context = peer;
    QuicConnection_Bind(&peer->connection, &connection_events);
    QuicControlChannel_Reset(&peer->control);
    peer->original_dcid = initial_header.dcid;
    memcpy(&peer->remote_address, address, address_length);
    peer->remote_address_length = address_length;
    peer->peer_id = self->next_peer_id++;

    if (!QuicServerSecurity_NewSession(&self->security, &peer->session, QuicConnection_Ref(&peer->connection))) {
        return NULL;
    }

    makePeerPath(self, peer, &path);
    if (!QuicConnection_OpenServer(&peer->connection, peer->session, &path, &initial_header)) {
        QuicServerSecurity_FreeSession(peer->session);
        peer->session = NULL;
        return NULL;
    }

    return peer;
}

static void makePeerPath(QuicServerTransport *self, QuicServerPeer *peer, ngtcp2_path *path)
{
    path->local.addr = (ngtcp2_sockaddr *)&self->local_address;
    path->local.addrlen = self->local_address_length;
    path->remote.addr = (ngtcp2_sockaddr *)&peer->remote_address;
    path->remote.addrlen = peer->remote_address_length;
    path->user_data = NULL;
}

/* ---------- private: egress ---------- */

static bool flushPeer(QuicServerTransport *self, QuicServerPeer *peer, const uint8_t *datagram, size_t datagram_size)
{
    QuicEgressSink sink = { peer, sendPacketToPeer };
    bool accepted = true;

    if (datagram != NULL) {
        if (!QuicEgress_FlushDatagram(&peer->connection, &sink, datagram, datagram_size, &accepted)) {
            teardownPeer(self, peer, true);
            return false;
        }
    }

    if (!QuicEgress_Drain(&peer->connection, &peer->control, &sink)) {
        teardownPeer(self, peer, true);
        return false;
    }

    rearmTimer(self);

    return accepted;
}

static void sendPacketToPeer(void *context, const uint8_t *packet, size_t size)
{
    QuicServerPeer *peer = context;

    sendToPeer(peer->transport, peer, packet, size);
}

static void sendToPeer(QuicServerTransport *self, QuicServerPeer *peer, const uint8_t *packet, size_t size)
{
    sendto(
        self->udp_fd,
        packet,
        size,
        0,
        (const struct sockaddr *)&peer->remote_address,
        peer->remote_address_length
    );
}

static void rearmTimer(QuicServerTransport *self)
{
    struct itimerspec timer_specification;
    uint64_t earliest_ns = UINT64_MAX;
    uint64_t expiry_ns;
    uint8_t i;

    for(i=0; i<QUIC_SERVER_PEERS_MAX; i++) {
        if (!QuicConnection_IsOpen(&self->peers[i].connection)) {
            continue;
        }
        expiry_ns = QuicConnection_NextExpiryNs(&self->peers[i].connection);
        if (expiry_ns < earliest_ns) {
            earliest_ns = expiry_ns;
        }
    }

    memset(&timer_specification, 0, sizeof(timer_specification));
    if (earliest_ns == UINT64_MAX) {
        timerfd_settime(self->timer_fd, 0, &timer_specification, NULL);
        return;
    }

    timer_specification.it_value.tv_sec = (time_t)(earliest_ns / NGTCP2_SECONDS);
    timer_specification.it_value.tv_nsec = (long)(earliest_ns % NGTCP2_SECONDS);
    timerfd_settime(self->timer_fd, TFD_TIMER_ABSTIME, &timer_specification, NULL);
}

static void teardownPeer(QuicServerTransport *self, QuicServerPeer *peer, bool notify)
{
    bool was_connected = peer->connected;
    uint32_t peer_id = peer->peer_id;

    QuicConnection_Close(&peer->connection);
    QuicServerSecurity_FreeSession(peer->session);
    peer->session = NULL;
    QuicControlChannel_Reset(&peer->control);
    peer->connected = false;
    peer->close_pending = false;

    if (notify && was_connected) {
        self->events.on_peer_disconnected(self->events.context, peer_id, Clock_RealtimeUs());
    }
}

/* ---------- private: connection events ---------- */

static void capturePeerFingerprint(QuicServerPeer *peer)
{
    const gnutls_datum_t *certificates;
    unsigned int certificate_count;

    certificates = gnutls_certificate_get_peers(peer->session, &certificate_count);
    if (certificates == NULL || certificate_count == 0) {
        return;
    }

    TlsIdentity_FingerprintOfDer(&certificates[0], peer->fingerprint_hex);
}

static void dispatchControlMessages(QuicServerTransport *self, QuicServerPeer *peer)
{
    const uint8_t *message;
    size_t message_size;

    for (;;) {
        message_size = QuicControlChannel_NextMessage(&peer->control, &message);
        if (message_size == 0) {
            return;
        }

        self->events.on_peer_control(
            self->events.context,
            peer->peer_id,
            message,
            message_size,
            Clock_RealtimeUs()
        );
        QuicControlChannel_ConsumeMessage(&peer->control, message_size);
    }
}

static void onHandshakeCompleted(void *context)
{
    QuicServerPeer *peer = context;

    capturePeerFingerprint(peer);
    peer->connected = true;
    peer->transport->events.on_peer_connected(
        peer->transport->events.context,
        peer->peer_id,
        peer->fingerprint_hex[0] != '\0' ? peer->fingerprint_hex : NULL,
        false
    );
}

static void onDatagram(void *context, const uint8_t *data, size_t size)
{
    QuicServerPeer *peer = context;

    peer->transport->events.on_peer_frame(peer->transport->events.context, peer->peer_id, data, size);
}

static void onStreamData(void *context, int64_t stream_id, const uint8_t *data, size_t size)
{
    QuicServerPeer *peer = context;

    if (peer->control.stream_id == QUIC_CONTROL_NO_STREAM) {
        peer->control.stream_id = stream_id;
    }
    if (stream_id != peer->control.stream_id) {
        return;
    }
    if (!QuicControlChannel_QueueRx(&peer->control, data, size)) {
        return;
    }

    dispatchControlMessages(peer->transport, peer);
    QuicConnection_ExtendStreamCredit(&peer->connection, stream_id, size);
}

static void onStreamAcked(void *context, int64_t stream_id, uint64_t acked_end_offset)
{
    QuicServerPeer *peer = context;

    if (stream_id != peer->control.stream_id) {
        return;
    }

    QuicControlChannel_MarkAcked(&peer->control, acked_end_offset);
}
