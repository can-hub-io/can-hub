#include "platform/linux/quic/quic_server_transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/timerfd.h>

#include "platform/linux/clock/clock.h"
#include "platform/linux/quic/quic_egress.h"
#include "platform/linux/quic/quic_udp_gso.h"
#include "platform/linux/shared/handshake_limits.h"
#include "platform/linux/shared/listen_endpoint.h"
#include "platform/linux/shared/log.h"
#include "protocol/message_header.h"

#define UDP_PACKET_BUFFER_SIZE 1452
#define UDP_SOCKET_BUFFER_BYTES (4 * 1024 * 1024)

static bool portSendControl(void *context, uint32_t peer_id, const uint8_t *data, size_t size);
static bool portSendFrame(void *context, uint32_t peer_id, uint8_t channel, const uint8_t *data, size_t size);
static bool portFrameFits(void *context, uint32_t peer_id, uint8_t channel, size_t size);
static void portSetChannelMode(void *context, uint32_t peer_id, uint8_t channel, bool reliable);
static void portClosePeer(void *context, uint32_t peer_id);
static QuicServerPeer *findPeerById(QuicServerTransport *self, uint32_t peer_id);
static QuicServerPeer *findPeerByDcid(QuicServerTransport *self, const uint8_t *packet, size_t packet_size);
static QuicServerPeer *acceptPeer(
    QuicServerTransport *self,
    const struct sockaddr_storage *address,
    socklen_t address_length,
    const uint8_t *packet,
    size_t packet_size
);
static QuicServerPeer *openPeer(
    QuicServerTransport *self,
    const ngtcp2_pkt_hd *initial_header,
    const struct sockaddr_storage *address,
    socklen_t address_length,
    const ngtcp2_cid *retried_original_dcid
);
static void sendRetry(
    QuicServerTransport *self,
    const ngtcp2_pkt_hd *initial_header,
    const struct sockaddr_storage *address,
    socklen_t address_length
);
static bool admitsHandshake(const QuicServerTransport *self, const char *origin_host);
static uint8_t countHandshakes(const QuicServerTransport *self);
static bool isHandshaking(const QuicServerPeer *peer);
static QuicServerPeer *findFreePeer(QuicServerTransport *self);
static void makePacketPath(
    const QuicServerTransport *self,
    const struct sockaddr_storage *address,
    socklen_t address_length,
    ngtcp2_path *path
);
static void followValidatedPath(QuicServerPeer *peer);
static ssize_t receivePacket(
    QuicServerTransport *self,
    uint8_t *packet,
    size_t packet_size,
    struct sockaddr_storage *address,
    socklen_t *address_length
);
static void captureReceiveOverflows(QuicServerTransport *self, struct msghdr *message);
static void dispatchPacket(
    QuicServerTransport *self,
    const uint8_t *packet,
    size_t packet_size,
    const struct sockaddr_storage *address,
    socklen_t address_length
);
static bool stageDatagram(QuicServerTransport *self, QuicServerPeer *peer, const uint8_t *data, size_t size);
static bool deferFlush(const QuicServerTransport *self, const QuicServerPeer *peer);
static void flushStagedPeers(QuicServerTransport *self);
static bool flushPeer(QuicServerTransport *self, QuicServerPeer *peer);
static void notifyWritable(QuicServerTransport *self, QuicServerPeer *peer);
static void sendPacketToPeer(void *context, const uint8_t *data, size_t size, size_t segment_size);
static void sendToPeer(QuicServerTransport *self, QuicServerPeer *peer, const uint8_t *data, size_t size, size_t segment_size);
static void rearmTimer(QuicServerTransport *self);
static void teardownPeer(QuicServerTransport *self, QuicServerPeer *peer, bool notify);
static void dispatchControlMessages(QuicServerTransport *self, QuicServerPeer *peer);
static void capturePeerFingerprint(QuicServerPeer *peer);
static void onHandshakeCompleted(void *context);
static void onDatagram(void *context, const uint8_t *data, size_t size);
static void onStreamData(void *context, int64_t stream_id, const uint8_t *data, size_t size);
static void onStreamAcked(void *context, int64_t stream_id, uint64_t acked_end_offset);
static void onStreamFinished(void *context, int64_t stream_id);
static void onStreamClosed(void *context, int64_t stream_id);
static void openReliableChannel(QuicServerPeer *peer, uint8_t channel);
static void closeReliableChannel(QuicServerPeer *peer, uint8_t channel);
static void receiveControlData(QuicServerPeer *peer, const uint8_t *data, size_t size);
static bool serverFrameSink(void *context, const uint8_t *frame, size_t size);
static void retryStalledReliableStreams(QuicServerTransport *self);

static const char *cipherSuiteOrUnknown(const char *name);

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
    struct sockaddr_storage address;
    socklen_t address_length;
    int32_t address_family;
    int32_t buffer_bytes;
    int32_t report_overflows = 1;

    memset(self, 0, sizeof(*self));
    self->port.context = self;
    self->port.send_control = portSendControl;
    self->port.send_frame = portSendFrame;
    self->port.frame_fits = portFrameFits;
    self->port.set_channel_mode = portSetChannelMode;
    self->port.close_peer = portClosePeer;
    self->events = *events;
    self->next_peer_id = peer_id_base;
    QuicRetry_Init(&self->retry);

    if (!QuicServerSecurity_Init(&self->security, certificate_file, key_file)) {
        return false;
    }

    self->udp_fd = ListenEndpoint_OpenSocket(SOCK_DGRAM, &address_family);
    self->timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (self->udp_fd < 0 || self->timer_fd < 0) {
        return false;
    }

    buffer_bytes = UDP_SOCKET_BUFFER_BYTES;
    setsockopt(self->udp_fd, SOL_SOCKET, SO_RCVBUF, &buffer_bytes, sizeof(buffer_bytes));
    setsockopt(self->udp_fd, SOL_SOCKET, SO_SNDBUF, &buffer_bytes, sizeof(buffer_bytes));
    setsockopt(self->udp_fd, SOL_SOCKET, SO_RXQ_OVFL, &report_overflows, sizeof(report_overflows));

    if (!ListenEndpoint_BuildSockaddr(address_family, bind_address, port, &address, &address_length)) {
        return false;
    }
    if (bind(self->udp_fd, (struct sockaddr *)&address, address_length) < 0) {
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
    ssize_t bytes_received;

    for (;;) {
        bytes_received = receivePacket(self, packet, sizeof(packet), &peer_address, &peer_address_length);
        if (bytes_received <= 0) {
            break;
        }

        dispatchPacket(self, packet, (size_t)bytes_received, &peer_address, peer_address_length);
        flushStagedPeers(self);
    }

    retryStalledReliableStreams(self);
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
        self->dispatching_peer = peer;
        if (!QuicConnection_HandleExpiry(&peer->connection)) {
            self->dispatching = false;
            self->dispatching_peer = NULL;
            teardownPeer(self, peer, true);
            continue;
        }
        self->dispatching = false;
        self->dispatching_peer = NULL;

        if (peer->close_pending) {
            flushPeer(self, peer);
            teardownPeer(self, peer, false);
            continue;
        }
        flushPeer(self, peer);
        notifyWritable(self, peer);
    }

    flushStagedPeers(self);
    retryStalledReliableStreams(self);
    rearmTimer(self);
}

void QuicServerTransport_Cork(QuicServerTransport *self)
{
    self->corked = true;
}

void QuicServerTransport_Uncork(QuicServerTransport *self)
{
    self->corked = false;
    flushStagedPeers(self);
}

uint32_t QuicServerTransport_ReceiveOverflows(const QuicServerTransport *self)
{
    return self->receive_overflows;
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

    return flushPeer(self, peer);
}

static bool portSendFrame(void *context, uint32_t peer_id, uint8_t channel, const uint8_t *data, size_t size)
{
    QuicServerTransport *self = context;
    QuicServerPeer *peer = findPeerById(self, peer_id);
    QuicReliableStream *reliable;

    if (peer == NULL || !peer->connected || peer->close_pending) {
        return false;
    }

    reliable = QuicReliableStreams_FindByChannel(&peer->reliable_streams, channel);
    if (reliable == NULL) {
        return stageDatagram(self, peer, data, size);
    }
    if (!QuicControlChannel_QueueTx(&reliable->stream, data, size)) {
        return false;
    }
    if (self->dispatching && peer == self->dispatching_peer) {
        return true;
    }

    return flushPeer(self, peer);
}

static bool portFrameFits(void *context, uint32_t peer_id, uint8_t channel, size_t size)
{
    QuicServerTransport *self = context;
    QuicServerPeer *peer = findPeerById(self, peer_id);
    QuicReliableStream *reliable;

    if (peer == NULL || !peer->connected || peer->close_pending) {
        return false;
    }

    reliable = QuicReliableStreams_FindByChannel(&peer->reliable_streams, channel);

    return reliable == NULL || QuicControlChannel_CanQueue(&reliable->stream, size);
}

static void portSetChannelMode(void *context, uint32_t peer_id, uint8_t channel, bool reliable)
{
    QuicServerTransport *self = context;
    QuicServerPeer *peer = findPeerById(self, peer_id);

    if (peer == NULL || !peer->connected) {
        return;
    }
    if (!reliable) {
        closeReliableChannel(peer, channel);
        return;
    }

    openReliableChannel(peer, channel);
}

static void openReliableChannel(QuicServerPeer *peer, uint8_t channel)
{
    if (QuicReliableStreams_FindByChannel(&peer->reliable_streams, channel) != NULL) {
        return;
    }
    if (QuicReliableStreams_Open(&peer->reliable_streams, &peer->connection, channel) == NULL) {
        LOG_WARN("peer %u reliable channel %u could not open a stream", peer->peer_id, channel);
        return;
    }

    LOG_INFO("peer %u reliable channel %u opened", peer->peer_id, channel);
}

static void closeReliableChannel(QuicServerPeer *peer, uint8_t channel)
{
    if (QuicReliableStreams_Close(&peer->reliable_streams, channel) == NULL) {
        return;
    }

    LOG_INFO("peer %u reliable channel %u closing", peer->peer_id, channel);
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

static QuicServerPeer *acceptPeer(
    QuicServerTransport *self,
    const struct sockaddr_storage *address,
    socklen_t address_length,
    const uint8_t *packet,
    size_t packet_size
)
{
    ngtcp2_pkt_hd initial_header;
    ngtcp2_cid original_dcid;
    bool retried;

    if (ngtcp2_accept(&initial_header, packet, packet_size) != 0) {
        return NULL;
    }

    retried = QuicRetry_HasToken(&initial_header);
    if (retried && !QuicRetry_Verify(&self->retry, &initial_header, address, address_length, &original_dcid)) {
        return NULL;
    }
    if (!retried && countHandshakes(self) >= QUIC_SERVER_RETRY_THRESHOLD) {
        sendRetry(self, &initial_header, address, address_length);
        return NULL;
    }

    return openPeer(self, &initial_header, address, address_length, retried ? &original_dcid : NULL);
}

static QuicServerPeer *openPeer(
    QuicServerTransport *self,
    const ngtcp2_pkt_hd *initial_header,
    const struct sockaddr_storage *address,
    socklen_t address_length,
    const ngtcp2_cid *retried_original_dcid
)
{
    QuicConnectionEvents connection_events = {
        .on_handshake_completed = onHandshakeCompleted,
        .on_datagram = onDatagram,
        .on_stream_data = onStreamData,
        .on_stream_acked = onStreamAcked,
        .on_stream_finished = onStreamFinished,
        .on_stream_closed = onStreamClosed,
    };
    char origin_host[QUIC_SERVER_ORIGIN_SIZE];
    QuicServerPeer *peer;
    ngtcp2_path path;

    ListenEndpoint_FormatHost(address, origin_host, sizeof(origin_host));
    if (!admitsHandshake(self, origin_host)) {
        return NULL;
    }
    peer = findFreePeer(self);
    if (peer == NULL) {
        return NULL;
    }

    memset(peer, 0, sizeof(*peer));
    peer->transport = self;
    QuicDatagramBacklog_Init(&peer->egress_backlog, peer->egress_slots, QUIC_CORKED_DATAGRAMS_MAX);
    connection_events.context = peer;
    QuicConnection_Bind(&peer->connection, &connection_events);
    QuicControlChannel_Reset(&peer->control);
    peer->original_dcid = initial_header->dcid;
    memcpy(&peer->remote_address, address, address_length);
    peer->remote_address_length = address_length;
    memcpy(peer->origin_host, origin_host, sizeof(peer->origin_host));
    peer->peer_id = self->next_peer_id++;

    if (!QuicServerSecurity_NewSession(&self->security, &peer->session, QuicConnection_Ref(&peer->connection))) {
        return NULL;
    }

    makePacketPath(self, address, address_length, &path);
    if (!QuicConnection_OpenServer(&peer->connection, &peer->session.tls_context, &path, initial_header, retried_original_dcid)) {
        QuicServerSecurity_FreeSession(&peer->session);
        return NULL;
    }

    LOG_DEBUG("quic: handshake %u from %s", peer->peer_id, peer->origin_host);

    return peer;
}

static void sendRetry(
    QuicServerTransport *self,
    const ngtcp2_pkt_hd *initial_header,
    const struct sockaddr_storage *address,
    socklen_t address_length
)
{
    uint8_t packet[UDP_PACKET_BUFFER_SIZE];
    ngtcp2_ssize packet_size = QuicRetry_Write(&self->retry, initial_header, address, address_length, packet, sizeof(packet));

    if (packet_size <= 0) {
        return;
    }

    LOG_DEBUG("quic: handshakes in flight, sent a retry");
    QuicUdpGso_Send(
        self->udp_fd,
        (const struct sockaddr *)address,
        address_length,
        packet,
        (size_t)packet_size,
        (size_t)packet_size,
        &self->gso_unsupported
    );
}

static bool admitsHandshake(const QuicServerTransport *self, const char *origin_host)
{
    const QuicServerPeer *peer;
    uint8_t handshaking = 0;
    uint8_t from_host = 0;
    uint8_t i;

    for(i=0; i<QUIC_SERVER_PEERS_MAX; i++) {
        peer = &self->peers[i];
        if (!isHandshaking(peer)) {
            continue;
        }
        handshaking++;
        if (strcmp(peer->origin_host, origin_host) == 0) {
            from_host++;
        }
    }

    return handshaking < QUIC_SERVER_HANDSHAKES_MAX && from_host < HANDSHAKES_PER_HOST_MAX;
}

static uint8_t countHandshakes(const QuicServerTransport *self)
{
    uint8_t handshaking = 0;
    uint8_t i;

    for(i=0; i<QUIC_SERVER_PEERS_MAX; i++) {
        if (isHandshaking(&self->peers[i])) {
            handshaking++;
        }
    }

    return handshaking;
}

static bool isHandshaking(const QuicServerPeer *peer)
{
    return QuicConnection_IsOpen(&peer->connection) && !peer->connected;
}

static QuicServerPeer *findFreePeer(QuicServerTransport *self)
{
    uint8_t i;

    for(i=0; i<QUIC_SERVER_PEERS_MAX; i++) {
        if (!QuicConnection_IsOpen(&self->peers[i].connection)) {
            return &self->peers[i];
        }
    }

    return NULL;
}

static void makePacketPath(
    const QuicServerTransport *self,
    const struct sockaddr_storage *address,
    socklen_t address_length,
    ngtcp2_path *path
)
{
    path->local.addr = (ngtcp2_sockaddr *)&self->local_address;
    path->local.addrlen = self->local_address_length;
    path->remote.addr = (ngtcp2_sockaddr *)address;
    path->remote.addrlen = address_length;
    path->user_data = NULL;
}

static void followValidatedPath(QuicServerPeer *peer)
{
    const ngtcp2_path *path = QuicConnection_Path(&peer->connection);

    memcpy(&peer->remote_address, path->remote.addr, path->remote.addrlen);
    peer->remote_address_length = path->remote.addrlen;
}

/* ---------- private: egress ---------- */

static ssize_t receivePacket(
    QuicServerTransport *self,
    uint8_t *packet,
    size_t packet_size,
    struct sockaddr_storage *address,
    socklen_t *address_length
)
{
    uint8_t control_buffer[CMSG_SPACE(sizeof(uint32_t))];
    struct iovec io_vector = { packet, packet_size };
    struct msghdr message;
    ssize_t bytes_received;

    memset(&message, 0, sizeof(message));
    message.msg_name = address;
    message.msg_namelen = sizeof(*address);
    message.msg_iov = &io_vector;
    message.msg_iovlen = 1;
    message.msg_control = control_buffer;
    message.msg_controllen = sizeof(control_buffer);

    bytes_received = recvmsg(self->udp_fd, &message, 0);
    if (bytes_received > 0) {
        *address_length = message.msg_namelen;
        captureReceiveOverflows(self, &message);
    }

    return bytes_received;
}

static void captureReceiveOverflows(QuicServerTransport *self, struct msghdr *message)
{
    struct cmsghdr *control_message;

    for(control_message=CMSG_FIRSTHDR(message); control_message!=NULL; control_message=CMSG_NXTHDR(message, control_message)) {
        if (control_message->cmsg_level == SOL_SOCKET && control_message->cmsg_type == SO_RXQ_OVFL) {
            memcpy(&self->receive_overflows, CMSG_DATA(control_message), sizeof(self->receive_overflows));
        }
    }
}

static void dispatchPacket(
    QuicServerTransport *self,
    const uint8_t *packet,
    size_t packet_size,
    const struct sockaddr_storage *address,
    socklen_t address_length
)
{
    QuicServerPeer *peer;
    ngtcp2_path path;

    peer = findPeerByDcid(self, packet, packet_size);
    if (peer == NULL) {
        peer = acceptPeer(self, address, address_length, packet, packet_size);
        if (peer == NULL) {
            return;
        }
    }

    makePacketPath(self, address, address_length, &path);
    self->dispatching = true;
    self->dispatching_peer = peer;
    if (!QuicConnection_ReadPacket(&peer->connection, &path, packet, packet_size)) {
        self->dispatching = false;
        self->dispatching_peer = NULL;
        teardownPeer(self, peer, true);
        return;
    }
    self->dispatching = false;
    self->dispatching_peer = NULL;
    followValidatedPath(peer);

    if (peer->close_pending) {
        flushPeer(self, peer);
        teardownPeer(self, peer, false);
        return;
    }
    flushPeer(self, peer);
    notifyWritable(self, peer);
}

static bool stageDatagram(QuicServerTransport *self, QuicServerPeer *peer, const uint8_t *data, size_t size)
{
    if (peer->egress_blocked || !QuicDatagramBacklog_Push(&peer->egress_backlog, data, size)) {
        return false;
    }
    if (!deferFlush(self, peer)) {
        flushPeer(self, peer);
    }

    return true;
}

static bool deferFlush(const QuicServerTransport *self, const QuicServerPeer *peer)
{
    if (self->dispatching && peer == self->dispatching_peer) {
        return true;
    }
    if (QuicDatagramBacklog_Count(&peer->egress_backlog) >= QUIC_CORKED_DATAGRAMS_MAX) {
        return false;
    }

    return self->corked || self->dispatching;
}

static void flushStagedPeers(QuicServerTransport *self)
{
    QuicServerPeer *peer;
    uint8_t i;

    for(i=0; i<QUIC_SERVER_PEERS_MAX; i++) {
        peer = &self->peers[i];
        if (!peer->connected || peer->egress_blocked || QuicDatagramBacklog_IsEmpty(&peer->egress_backlog)) {
            continue;
        }
        flushPeer(self, peer);
    }
}

static bool flushPeer(QuicServerTransport *self, QuicServerPeer *peer)
{
    QuicEgressSink sink = { peer, sendPacketToPeer };

    if (!QuicEgress_FlushDatagrams(&peer->connection, &sink, &peer->egress_backlog)) {
        teardownPeer(self, peer, true);
        return false;
    }
    peer->egress_blocked = !QuicDatagramBacklog_IsEmpty(&peer->egress_backlog);

    if (!QuicEgress_Drain(&peer->connection, &peer->control, &sink)) {
        teardownPeer(self, peer, true);
        return false;
    }

    if (!QuicReliableStreams_Drain(&peer->reliable_streams, &peer->connection, &sink)) {
        teardownPeer(self, peer, true);
        return false;
    }

    rearmTimer(self);

    return true;
}

static void retryStalledReliableStreams(QuicServerTransport *self)
{
    uint8_t i;

    for(i=0; i<QUIC_SERVER_PEERS_MAX; i++) {
        if (QuicConnection_IsOpen(&self->peers[i].connection)) {
            QuicReliableStreams_RetryDrain(&self->peers[i].reliable_streams, &self->peers[i].connection, serverFrameSink, &self->peers[i]);
        }
    }
    for(i=0; i<QUIC_SERVER_PEERS_MAX; i++) {
        if (QuicConnection_IsOpen(&self->peers[i].connection)) {
            flushPeer(self, &self->peers[i]);
        }
    }
}

static void notifyWritable(QuicServerTransport *self, QuicServerPeer *peer)
{
    if (self->events.on_peer_writable == NULL || !peer->connected || peer->close_pending) {
        return;
    }

    self->events.on_peer_writable(self->events.context, peer->peer_id);
}

static void sendPacketToPeer(void *context, const uint8_t *data, size_t size, size_t segment_size)
{
    QuicServerPeer *peer = context;

    sendToPeer(peer->transport, peer, data, size, segment_size);
}

static void sendToPeer(QuicServerTransport *self, QuicServerPeer *peer, const uint8_t *data, size_t size, size_t segment_size)
{
    QuicUdpGso_Send(
        self->udp_fd,
        (const struct sockaddr *)&peer->remote_address,
        peer->remote_address_length,
        data,
        size,
        segment_size,
        &self->gso_unsupported
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

    QuicServerSecurity_FreeSession(&peer->session);
    QuicConnection_Close(&peer->connection);
    QuicControlChannel_Reset(&peer->control);
    QuicReliableStreams_Reset(&peer->reliable_streams);
    QuicDatagramBacklog_Reset(&peer->egress_backlog);
    peer->connected = false;
    peer->close_pending = false;
    peer->egress_blocked = false;

    if (notify && was_connected) {
        self->events.on_peer_disconnected(self->events.context, peer_id, Clock_MonotonicUs());
    }
}

/* ---------- private: connection events ---------- */

static void capturePeerFingerprint(QuicServerPeer *peer)
{
    snprintf(
        peer->fingerprint_hex,
        TLS_IDENTITY_FINGERPRINT_HEX_SIZE,
        "%s",
        QuicConnection_PeerCertificate(&peer->connection)->fingerprint
    );
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
            Clock_MonotonicUs()
        );
        QuicControlChannel_ConsumeMessage(&peer->control, message_size);
    }
}

static void onHandshakeCompleted(void *context)
{
    QuicServerPeer *peer = context;
    HubPeerConnectInfo info;
    char origin[QUIC_SERVER_ORIGIN_SIZE];
    char origin_host[QUIC_SERVER_ORIGIN_SIZE];

    capturePeerFingerprint(peer);
    peer->connected = true;
    ListenEndpoint_FormatOrigin(&peer->remote_address, origin, sizeof(origin));
    ListenEndpoint_FormatHost(&peer->remote_address, origin_host, sizeof(origin_host));
    LOG_INFO("peer %u on quic:// from %s negotiated %s", peer->peer_id, origin,
             cipherSuiteOrUnknown(QuicConnection_CipherSuiteName(&peer->connection)));
    info.fingerprint_hex = peer->fingerprint_hex[0] != '\0' ? peer->fingerprint_hex : NULL;
    info.origin = origin;
    info.origin_host = origin_host;
    info.transport_kind = kPEER_TRANSPORT_QUIC;
    info.local = false;
    peer->transport->events.on_peer_connected(
        peer->transport->events.context,
        peer->peer_id,
        &info,
        Clock_MonotonicUs()
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
    QuicReliableStream *reliable;

    if (peer->control.stream_id == QUIC_CONTROL_NO_STREAM) {
        peer->control.stream_id = stream_id;
    }
    if (stream_id == peer->control.stream_id) {
        receiveControlData(peer, data, size);
        QuicConnection_ExtendStreamCredit(&peer->connection, stream_id, size);
        return;
    }

    reliable = QuicReliableStreams_FindById(&peer->reliable_streams, stream_id);
    if (reliable == NULL) {
        reliable = QuicReliableStreams_Adopt(&peer->reliable_streams, stream_id);
    }
    if (reliable != NULL) {
        /* Receive extends credit only for relayed bytes; un-relayable bytes stay
         * framed and withhold credit, backpressuring the sender instead of dropping. */
        QuicReliableStreams_Receive(reliable, &peer->connection, data, size, serverFrameSink, peer);
        return;
    }
    QuicConnection_ExtendStreamCredit(&peer->connection, stream_id, size);
}

static void onStreamAcked(void *context, int64_t stream_id, uint64_t acked_end_offset)
{
    QuicServerPeer *peer = context;
    QuicReliableStream *reliable;

    if (stream_id == peer->control.stream_id) {
        QuicControlChannel_MarkAcked(&peer->control, acked_end_offset);
        return;
    }

    reliable = QuicReliableStreams_FindById(&peer->reliable_streams, stream_id);
    if (reliable != NULL) {
        QuicControlChannel_MarkAcked(&reliable->stream, acked_end_offset);
    }
}

static void onStreamFinished(void *context, int64_t stream_id)
{
    QuicServerPeer *peer = context;

    QuicReliableStreams_Finish(&peer->reliable_streams, stream_id);
}

static void onStreamClosed(void *context, int64_t stream_id)
{
    QuicServerPeer *peer = context;
    QuicReliableStream *reliable = QuicReliableStreams_FindById(&peer->reliable_streams, stream_id);
    uint8_t channel;

    if (reliable == NULL) {
        return;
    }

    channel = reliable->channel;
    QuicReliableStreams_Release(&peer->reliable_streams, stream_id);
    LOG_INFO("peer %u reliable channel %u closed", peer->peer_id, channel);
}

static void receiveControlData(QuicServerPeer *peer, const uint8_t *data, size_t size)
{
    size_t offset = 0;
    size_t taken;

    while (offset < size) {
        taken = QuicControlChannel_QueueRx(&peer->control, data + offset, size - offset);
        offset += taken;
        dispatchControlMessages(peer->transport, peer);
        if (taken == 0) {
            break;
        }
    }
}

static bool serverFrameSink(void *context, const uint8_t *frame, size_t size)
{
    QuicServerPeer *peer = context;

    return peer->transport->events.on_peer_frame(peer->transport->events.context, peer->peer_id, frame, size);
}

static const char *cipherSuiteOrUnknown(const char *name)
{
    return name != NULL ? name : "unknown";
}
