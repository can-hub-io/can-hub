#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "platform/windows/tcp/tcp_channel.h"
#include "agent/ports/transport_events.h"
#include "agent/ports/transport_port.h"

#define TCP_HOST_MAX 256
#define TCP_PORT_TEXT_MAX 16

/*
 * Plaintext TCP transport over winsock: both planes share the single
 * stream. Same contract as the POSIX adapter; unix sockets are not
 * supported on Windows and InitUnix always fails.
 */
typedef struct {
    TransportPort port;
    TransportEvents events;
    char host[TCP_HOST_MAX];
    char port_text[TCP_PORT_TEXT_MAX];
    TcpChannel channel;
    bool connecting;
    bool connected;
} TcpClientTransport;

bool TcpClientTransport_Init(
    TcpClientTransport *self,
    const char *host,
    const char *port,
    const TransportEvents *events
);
bool TcpClientTransport_InitUnix(TcpClientTransport *self, const char *socket_path, const TransportEvents *events);
TransportPort *TcpClientTransport_Port(TcpClientTransport *self);
int32_t TcpClientTransport_Fd(const TcpClientTransport *self);
bool TcpClientTransport_WantsWritable(const TcpClientTransport *self);
void TcpClientTransport_OnReadable(TcpClientTransport *self);
void TcpClientTransport_OnWritable(TcpClientTransport *self);
