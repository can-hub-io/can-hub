#include "platform/windows/tls/tls_client_transport.h"

#include <stdio.h>
#include <string.h>

#include <winsock2.h>
#include <ws2tcpip.h>

#include "platform/linux/clock/clock.h"
#include "protocol/message_header.h"

static bool portConnect(void *context);
static void portDisconnect(void *context);
static bool portSendControl(void *context, const uint8_t *data, size_t size);
static bool portSendFrame(void *context, const uint8_t *data, size_t size);
static bool connectTcp(TlsClientTransport *self, int32_t *connected_fd);
static void pumpHandshake(TlsClientTransport *self);
static void dispatchMessages(TlsClientTransport *self);
static void closeConnection(TlsClientTransport *self, bool notify);

/* ---------- public ---------- */

bool TlsClientTransport_Init(
    TlsClientTransport *self,
    const char *host,
    const char *port,
    const TransportEvents *events,
    const TlsClientSecurityConfig *security_config
)
{
    memset(self, 0, sizeof(*self));
    self->port.context = self;
    self->port.connect = portConnect;
    self->port.disconnect = portDisconnect;
    self->port.send_control = portSendControl;
    self->port.send_frame = portSendFrame;
    self->events = *events;
    snprintf(self->host, TLS_CLIENT_HOST_MAX, "%s", host);
    snprintf(self->port_text, TLS_CLIENT_PORT_TEXT_MAX, "%s", port);
    TlsChannel_Reset(&self->channel);

    return TlsClientSecurity_Init(&self->security, security_config);
}

TransportPort *TlsClientTransport_Port(TlsClientTransport *self)
{
    return &self->port;
}

int32_t TlsClientTransport_Fd(const TlsClientTransport *self)
{
    return self->channel.fd;
}

bool TlsClientTransport_WantsWritable(const TlsClientTransport *self)
{
    if (!TlsChannel_IsBound(&self->channel)) {
        return false;
    }

    return self->connecting || TlsChannel_WantsWrite(&self->channel);
}

void TlsClientTransport_OnReadable(TlsClientTransport *self)
{
    if (!TlsChannel_IsBound(&self->channel)) {
        return;
    }

    if (!TlsChannel_IsEstablished(&self->channel)) {
        pumpHandshake(self);
        return;
    }

    if (!TlsChannel_Receive(&self->channel)) {
        dispatchMessages(self);
        closeConnection(self, true);
        return;
    }

    dispatchMessages(self);
}

void TlsClientTransport_OnWritable(TlsClientTransport *self)
{
    int32_t socket_error = 0;
    int error_length = sizeof(socket_error);

    if (!TlsChannel_IsBound(&self->channel)) {
        return;
    }

    if (self->connecting) {
        getsockopt((SOCKET)self->channel.fd, SOL_SOCKET, SO_ERROR, (char *)&socket_error, &error_length);
        if (socket_error != 0) {
            closeConnection(self, true);
            return;
        }

        self->connecting = false;
        pumpHandshake(self);
        return;
    }

    if (!TlsChannel_IsEstablished(&self->channel)) {
        pumpHandshake(self);
        return;
    }

    if (!TlsChannel_Flush(&self->channel)) {
        closeConnection(self, true);
    }
}

/* ---------- private: transport port ---------- */

static bool portConnect(void *context)
{
    TlsClientTransport *self = context;
    SSL *ssl;
    int32_t connected_fd;

    if (TlsChannel_IsBound(&self->channel)) {
        return true;
    }

    if (!connectTcp(self, &connected_fd)) {
        return false;
    }
    if (!TlsClientSecurity_NewSession(&self->security, self->host, &ssl)) {
        closesocket((SOCKET)connected_fd);
        return false;
    }

    TlsChannel_Bind(&self->channel, connected_fd, ssl);
    self->connecting = true;
    self->announced = false;

    return true;
}

static void portDisconnect(void *context)
{
    TlsClientTransport *self = context;

    closeConnection(self, false);
}

static bool portSendControl(void *context, const uint8_t *data, size_t size)
{
    TlsClientTransport *self = context;

    if (!self->announced) {
        return false;
    }
    if (!TlsChannel_Queue(&self->channel, data, size)) {
        return false;
    }
    if (!TlsChannel_Flush(&self->channel)) {
        closeConnection(self, true);
        return false;
    }

    return true;
}

static bool portSendFrame(void *context, const uint8_t *data, size_t size)
{
    TlsClientTransport *self = context;

    if (!self->announced) {
        return false;
    }
    if (TlsChannel_FreeTxSpace(&self->channel) < size) {
        return false;
    }

    return portSendControl(context, data, size);
}

/* ---------- private ---------- */

static bool connectTcp(TlsClientTransport *self, int32_t *connected_fd)
{
    struct addrinfo hints;
    struct addrinfo *resolved;
    SOCKET tcp_socket;
    u_long nonblocking = 1;
    int nodelay = 1;
    int connect_result;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(self->host, self->port_text, &hints, &resolved) != 0) {
        return false;
    }

    tcp_socket = socket(resolved->ai_family, resolved->ai_socktype, 0);
    if (tcp_socket == INVALID_SOCKET) {
        freeaddrinfo(resolved);
        return false;
    }
    ioctlsocket(tcp_socket, FIONBIO, &nonblocking);
    setsockopt(tcp_socket, IPPROTO_TCP, TCP_NODELAY, (const char *)&nodelay, sizeof(nodelay));

    connect_result = connect(tcp_socket, resolved->ai_addr, (int)resolved->ai_addrlen);
    freeaddrinfo(resolved);
    if (connect_result != 0 && WSAGetLastError() != WSAEWOULDBLOCK) {
        closesocket(tcp_socket);
        return false;
    }

    *connected_fd = (int32_t)tcp_socket;

    return true;
}

static void pumpHandshake(TlsClientTransport *self)
{
    if (!TlsChannel_Pump(&self->channel)) {
        closeConnection(self, true);
        return;
    }

    if (TlsChannel_IsEstablished(&self->channel) && !self->announced) {
        self->announced = true;
        self->events.on_connected(self->events.context);
    }
}

static void dispatchMessages(TlsClientTransport *self)
{
    MessageHeader header;
    const uint8_t *message;
    size_t message_size;

    for (;;) {
        message_size = MessageFramer_NextMessage(&self->channel.framer, &message);
        if (message_size == 0) {
            return;
        }

        MessageHeader_Decode(&header, message, message_size);
        if (header.type == kMESSAGE_TYPE_FRAME) {
            self->events.on_frame(self->events.context, message, message_size);
        } else {
            self->events.on_control(self->events.context, message, message_size, Clock_RealtimeUs());
        }
        MessageFramer_Consume(&self->channel.framer, message_size);
    }
}

static void closeConnection(TlsClientTransport *self, bool notify)
{
    int32_t connection_fd;

    if (!TlsChannel_IsBound(&self->channel)) {
        return;
    }

    connection_fd = self->channel.fd;
    TlsChannel_Close(&self->channel);
    closesocket((SOCKET)connection_fd);
    self->connecting = false;
    self->announced = false;

    if (notify) {
        self->events.on_disconnected(self->events.context, Clock_RealtimeUs());
    }
}
