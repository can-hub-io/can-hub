#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <ngtcp2/ngtcp2_crypto_picotls.h>

#include <sys/socket.h>

#include "platform/linux/quic/quic_connection.h"
#include "platform/linux/quic/quic_control_channel.h"
#include "platform/linux/quic/quic_datagram_backlog.h"
#include "platform/linux/quic/quic_egress.h"
#include "platform/linux/quic/quic_reliable_streams.h"
#include "platform/linux/quic/quic_retry.h"
#include "platform/linux/quic/quic_server_security.h"
#include "platform/linux/shared/tls_identity.h"
#include "hub/ports/hub_transport_events.h"
#include "hub/ports/hub_transport_port.h"

#define QUIC_SERVER_PEERS_MAX HUB_PEERS_MAX
#define QUIC_SERVER_HANDSHAKES_MAX (QUIC_SERVER_PEERS_MAX / 2)
#define QUIC_SERVER_RETRY_THRESHOLD 8
#define QUIC_SERVER_ORIGIN_SIZE 56

/*
 * QUIC server transport for the hub: one UDP socket multiplexing every
 * connection, peers matched by Destination Connection ID (survives client
 * migration and NAT rebinding; the remote address follows the latest
 * packet), one shared expiry timer armed to the earliest deadline. Control
 * plane on the client-opened bidirectional stream, data plane on datagrams.
 * Reliable channels ride dedicated bidirectional streams: the hub opens one
 * per channel (set_channel_mode) for that channel's frames; an incoming
 * stream that is neither the control nor a known reliable stream is adopted
 * for receive (its frames carry their own channel).
 * Datagrams for a peer are staged while one incoming packet (or one corked
 * event) is handled and leave packed into as few packets as fit; while a
 * flush leaves some behind the peer refuses new ones, so the core queues
 * them. ReceiveOverflows is the kernel's count of packets dropped because
 * the socket's receive buffer was full.
 * Admission: once QUIC_SERVER_RETRY_THRESHOLD handshakes are in flight, an
 * Initial without a valid Retry token gets a Retry instead of a slot; one
 * host may hold HANDSHAKES_PER_HOST_MAX handshakes and all of them together
 * half the table. The remote address is the path ngtcp2 has validated,
 * never the source of whichever packet arrived last.
 */
typedef struct QuicServerTransport QuicServerTransport;

typedef struct {
    QuicServerTransport *transport;
    QuicConnection connection;
    QuicServerSession session;
    QuicControlChannel control;
    QuicReliableStreamSet reliable_streams;
    QuicDatagramSlot egress_slots[QUIC_CORKED_DATAGRAMS_MAX];
    QuicDatagramBacklog egress_backlog;
    ngtcp2_cid original_dcid;
    char fingerprint_hex[TLS_IDENTITY_FINGERPRINT_HEX_SIZE];
    struct sockaddr_storage remote_address;
    socklen_t remote_address_length;
    char origin_host[QUIC_SERVER_ORIGIN_SIZE];
    uint32_t peer_id;
    bool connected;
    bool close_pending;
    bool egress_blocked;
} QuicServerPeer;

struct QuicServerTransport {
    HubTransportPort port;
    HubTransportEvents events;
    QuicServerSecurity security;
    QuicRetry retry;
    int32_t udp_fd;
    int32_t timer_fd;
    uint32_t next_peer_id;
    bool dispatching;
    bool corked;
    bool gso_unsupported;
    uint32_t receive_overflows;
    QuicServerPeer *dispatching_peer;
    struct sockaddr_storage local_address;
    socklen_t local_address_length;
    QuicServerPeer peers[QUIC_SERVER_PEERS_MAX];
};

bool QuicServerTransport_Init(
    QuicServerTransport *self,
    const char *bind_address,
    const char *port,
    const char *certificate_file,
    const char *key_file,
    uint32_t peer_id_base,
    const HubTransportEvents *events
);
HubTransportPort *QuicServerTransport_Port(QuicServerTransport *self);
int32_t QuicServerTransport_UdpFd(const QuicServerTransport *self);
int32_t QuicServerTransport_TimerFd(const QuicServerTransport *self);
void QuicServerTransport_OnUdpReadable(QuicServerTransport *self);
void QuicServerTransport_OnTimer(QuicServerTransport *self);
void QuicServerTransport_Cork(QuicServerTransport *self);
void QuicServerTransport_Uncork(QuicServerTransport *self);
uint32_t QuicServerTransport_ReceiveOverflows(const QuicServerTransport *self);
