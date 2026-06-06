#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "platform/linux/tcp/tcp_channel.h"
#include "hub/ports/hub_transport_events.h"
#include "hub/ports/hub_transport_port.h"

#define TCP_SERVER_PEERS_MAX 64

/*
 * Plaintext TCP server transport for the hub: accepts up to
 * TCP_SERVER_PEERS_MAX peers, one TcpChannel each, and dispatches incoming
 * messages by type to HubTransportEvents. The platform loop polls ListenFd
 * plus every bound SlotFd, watching EPOLLOUT for slots with pending TX.
 */
typedef struct {
    TcpChannel channel;
    uint32_t peer_id;
} TcpServerPeer;

typedef struct {
    HubTransportPort port;
    HubTransportEvents events;
    int32_t listen_fd;
    uint32_t next_peer_id;
    TcpServerPeer peers[TCP_SERVER_PEERS_MAX];
} TcpServerTransport;

bool TcpServerTransport_Init(
    TcpServerTransport *self,
    const char *port,
    uint32_t peer_id_base,
    const HubTransportEvents *events
);
bool TcpServerTransport_InitUnix(
    TcpServerTransport *self,
    const char *socket_path,
    uint32_t peer_id_base,
    const HubTransportEvents *events
);
HubTransportPort *TcpServerTransport_Port(TcpServerTransport *self);
int32_t TcpServerTransport_ListenFd(const TcpServerTransport *self);
int32_t TcpServerTransport_SlotFd(const TcpServerTransport *self, uint8_t slot);
bool TcpServerTransport_SlotWantsWritable(const TcpServerTransport *self, uint8_t slot);
void TcpServerTransport_OnAcceptReady(TcpServerTransport *self);
void TcpServerTransport_OnSlotReadable(TcpServerTransport *self, uint8_t slot);
void TcpServerTransport_OnSlotWritable(TcpServerTransport *self, uint8_t slot);
