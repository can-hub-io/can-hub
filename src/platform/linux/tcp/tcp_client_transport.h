#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "platform/linux/shared/message_framer.h"
#include "ports/transport_events.h"
#include "ports/transport_port.h"

#define TCP_HOST_MAX 256
#define TCP_PORT_TEXT_MAX 16
#define TCP_TX_BACKLOG_SIZE 8192

/*
 * Plaintext TCP transport: both planes share the single stream. Incoming
 * messages are reassembled and dispatched by type (FRAME to on_frame, the
 * rest to on_control). Partial writes are kept in a TX backlog so the
 * stream never corrupts; frames are dropped when the backlog cannot take
 * the whole message (best-effort plane). The connection fd is created per
 * connect — the platform loop must re-register it after each reconnect and
 * watch EPOLLOUT until the connection is established.
 */
typedef struct {
    TransportPort port;
    TransportEvents events;
    char host[TCP_HOST_MAX];
    char port_text[TCP_PORT_TEXT_MAX];
    int32_t tcp_fd;
    bool connecting;
    bool connected;
    MessageFramer framer;
    uint8_t tx_backlog[TCP_TX_BACKLOG_SIZE];
    size_t tx_used;
} TcpClientTransport;

bool TcpClientTransport_Init(
    TcpClientTransport *self,
    const char *host,
    const char *port,
    const TransportEvents *events
);
TransportPort *TcpClientTransport_Port(TcpClientTransport *self);
int32_t TcpClientTransport_Fd(const TcpClientTransport *self);
bool TcpClientTransport_WantsWritable(const TcpClientTransport *self);
void TcpClientTransport_OnReadable(TcpClientTransport *self);
void TcpClientTransport_OnWritable(TcpClientTransport *self);
