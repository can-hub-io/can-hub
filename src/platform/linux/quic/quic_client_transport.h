#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "platform/linux/quic/quic_client_security.h"
#include "platform/linux/quic/quic_connection.h"
#include "platform/linux/quic/quic_control_channel.h"
#include "platform/linux/quic/quic_udp_endpoint.h"
#include "agent/ports/transport_events.h"
#include "agent/ports/transport_port.h"

#define QUIC_HOST_MAX 256
#define QUIC_PORT_TEXT_MAX 16

/*
 * QUIC client adapter for the agent: implements TransportPort over ngtcp2.
 * Control plane on one bidirectional stream (QuicControlChannel), data plane
 * on QUIC datagrams. The platform loop polls UdpFd/TimerFd and calls
 * OnUdpReadable/OnTimer; transport events are pushed via QuicTransportEvents.
 */

typedef struct {
    char host[QUIC_HOST_MAX];
    char port_text[QUIC_PORT_TEXT_MAX];
} QuicServerEndpoint;

typedef struct {
    TransportPort port;
    TransportEvents events;
    QuicServerEndpoint server;
    QuicUdpEndpoint udp;
    QuicClientSecurity security;
    QuicClientSecurityConfig security_config;
    QuicControlChannel control;
    QuicConnection connection;
    bool connected;
} QuicClientTransport;

bool QuicClientTransport_Init(
    QuicClientTransport *self,
    const char *host,
    const char *port,
    const TransportEvents *events,
    const QuicClientSecurityConfig *security_config
);
TransportPort *QuicClientTransport_Port(QuicClientTransport *self);
int32_t QuicClientTransport_UdpFd(const QuicClientTransport *self);
int32_t QuicClientTransport_TimerFd(const QuicClientTransport *self);
void QuicClientTransport_OnUdpReadable(QuicClientTransport *self);
void QuicClientTransport_OnTimer(QuicClientTransport *self);
