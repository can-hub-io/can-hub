#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "platform/linux/tls/tls_channel.h"
#include "platform/linux/tls/tls_server_security.h"
#include "hub/ports/hub_transport_events.h"
#include "hub/ports/hub_transport_port.h"
#include "shared/slot_index.h"

#define TLS_SERVER_HANDSHAKES_MAX 64
#define TLS_SERVER_PEERS_MAX (HUB_PEERS_MAX + TLS_SERVER_HANDSHAKES_MAX)
#define TLS_SERVER_ORIGIN_SIZE 56
#define TLS_SERVER_PEER_ID_BUCKETS 2048

/*
 * TLS-over-TCP server transport for the hub: accepts up to
 * TLS_SERVER_PEERS_MAX peers, one TlsChannel each, requires a client
 * certificate and announces the peer with its fingerprint once the
 * handshake completes. The platform loop polls ListenFd plus every bound
 * SlotFd, watching EPOLLOUT for slots that want to write. Slots fill from
 * the lowest, so only those below SlotsTouched have ever held a peer and
 * need polling.
 */
typedef struct {
    TlsChannel channel;
    int32_t fd;
    uint32_t peer_id;
    bool announced;
    uint64_t handshake_deadline_us;
    char origin[TLS_SERVER_ORIGIN_SIZE];
    char origin_host[TLS_SERVER_ORIGIN_SIZE];
} TlsServerPeer;

typedef struct {
    HubTransportPort port;
    HubTransportEvents events;
    TlsServerSecurity security;
    int32_t listen_fd;
    uint32_t next_peer_id;
    uint16_t slots_touched;
    SlotIndexBucket peer_id_buckets[TLS_SERVER_PEER_ID_BUCKETS];
    SlotIndex slot_by_peer_id;
    TlsServerPeer peers[TLS_SERVER_PEERS_MAX];
} TlsServerTransport;

bool TlsServerTransport_Init(
    TlsServerTransport *self,
    const char *bind_address,
    const char *port,
    const char *certificate_file,
    const char *key_file,
    uint32_t peer_id_base,
    const HubTransportEvents *events
);
HubTransportPort *TlsServerTransport_Port(TlsServerTransport *self);
int32_t TlsServerTransport_ListenFd(const TlsServerTransport *self);
int32_t TlsServerTransport_SlotFd(const TlsServerTransport *self, uint16_t slot);
bool TlsServerTransport_SlotWantsWritable(const TlsServerTransport *self, uint16_t slot);
uint32_t TlsServerTransport_SlotPeerId(const TlsServerTransport *self, uint16_t slot);
uint16_t TlsServerTransport_SlotsTouched(const TlsServerTransport *self);
void TlsServerTransport_OnAcceptReady(TlsServerTransport *self);
void TlsServerTransport_OnSlotReadable(TlsServerTransport *self, uint16_t slot);
void TlsServerTransport_OnSlotWritable(TlsServerTransport *self, uint16_t slot);
void TlsServerTransport_Tick(TlsServerTransport *self, uint64_t now_us);
