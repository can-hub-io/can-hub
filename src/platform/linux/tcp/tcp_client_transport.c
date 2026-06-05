#include "platform/linux/tcp/tcp_client_transport.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include "platform/linux/clock/clock.h"
#include "protocol/message_header.h"

#define READ_CHUNK_SIZE 2048
#define NO_SOCKET (-1)

static bool portConnect(void *context);
static void portDisconnect(void *context);
static bool portSendControl(void *context, const uint8_t *data, size_t size);
static bool portSendFrame(void *context, const uint8_t *data, size_t size);
static bool queueBytes(TcpClientTransport *self, const uint8_t *data, size_t size);
static bool flushBacklog(TcpClientTransport *self);
static void dispatchMessages(TcpClientTransport *self);
static void closeConnection(TcpClientTransport *self, bool notify);

/* ---------- public ---------- */

bool TcpClientTransport_Init(
    TcpClientTransport *self,
    const char *host,
    const char *port,
    const TransportEvents *events
)
{
    memset(self, 0, sizeof(*self));
    self->port.context = self;
    self->port.connect = portConnect;
    self->port.disconnect = portDisconnect;
    self->port.send_control = portSendControl;
    self->port.send_frame = portSendFrame;
    self->events = *events;
    self->tcp_fd = NO_SOCKET;
    MessageFramer_Reset(&self->framer);
    snprintf(self->host, TCP_HOST_MAX, "%s", host);
    snprintf(self->port_text, TCP_PORT_TEXT_MAX, "%s", port);

    return true;
}

TransportPort *TcpClientTransport_Port(TcpClientTransport *self)
{
    return &self->port;
}

int32_t TcpClientTransport_Fd(const TcpClientTransport *self)
{
    return self->tcp_fd;
}

bool TcpClientTransport_WantsWritable(const TcpClientTransport *self)
{
    return self->connecting || self->tx_used > 0;
}

void TcpClientTransport_OnReadable(TcpClientTransport *self)
{
    uint8_t chunk[READ_CHUNK_SIZE];
    ssize_t bytes_received;

    if (self->tcp_fd == NO_SOCKET) {
        return;
    }

    for (;;) {
        bytes_received = recv(self->tcp_fd, chunk, sizeof(chunk), 0);
        if (bytes_received == 0 || (bytes_received < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            closeConnection(self, true);
            return;
        }
        if (bytes_received < 0) {
            return;
        }

        if (!MessageFramer_Push(&self->framer, chunk, (size_t)bytes_received)) {
            closeConnection(self, true);
            return;
        }
        dispatchMessages(self);
    }
}

void TcpClientTransport_OnWritable(TcpClientTransport *self)
{
    int32_t socket_error = 0;
    socklen_t error_length = sizeof(socket_error);

    if (self->tcp_fd == NO_SOCKET) {
        return;
    }

    if (self->connecting) {
        getsockopt(self->tcp_fd, SOL_SOCKET, SO_ERROR, &socket_error, &error_length);
        if (socket_error != 0) {
            closeConnection(self, true);
            return;
        }

        self->connecting = false;
        self->connected = true;
        self->events.on_connected(self->events.context);
        return;
    }

    flushBacklog(self);
}

/* ---------- private: transport port ---------- */

static bool portConnect(void *context)
{
    TcpClientTransport *self = context;
    struct addrinfo hints;
    struct addrinfo *resolved;
    int32_t connect_result;

    if (self->tcp_fd != NO_SOCKET) {
        return true;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(self->host, self->port_text, &hints, &resolved) != 0) {
        return false;
    }

    self->tcp_fd = socket(resolved->ai_family, resolved->ai_socktype | SOCK_NONBLOCK, 0);
    if (self->tcp_fd < 0) {
        freeaddrinfo(resolved);
        self->tcp_fd = NO_SOCKET;
        return false;
    }

    connect_result = connect(self->tcp_fd, resolved->ai_addr, resolved->ai_addrlen);
    freeaddrinfo(resolved);
    if (connect_result < 0 && errno != EINPROGRESS) {
        closeConnection(self, false);
        return false;
    }

    self->connecting = true;

    return true;
}

static void portDisconnect(void *context)
{
    TcpClientTransport *self = context;

    closeConnection(self, false);
}

static bool portSendControl(void *context, const uint8_t *data, size_t size)
{
    TcpClientTransport *self = context;

    if (!self->connected) {
        return false;
    }

    return queueBytes(self, data, size) && flushBacklog(self);
}

static bool portSendFrame(void *context, const uint8_t *data, size_t size)
{
    TcpClientTransport *self = context;

    if (!self->connected) {
        return false;
    }
    if (self->tx_used + size > TCP_TX_BACKLOG_SIZE) {
        return false;
    }

    return queueBytes(self, data, size) && flushBacklog(self);
}

/* ---------- private ---------- */

static bool queueBytes(TcpClientTransport *self, const uint8_t *data, size_t size)
{
    if (self->tx_used + size > TCP_TX_BACKLOG_SIZE) {
        return false;
    }

    memcpy(self->tx_backlog + self->tx_used, data, size);
    self->tx_used += size;

    return true;
}

static bool flushBacklog(TcpClientTransport *self)
{
    ssize_t bytes_sent;

    while (self->tx_used > 0) {
        bytes_sent = send(self->tcp_fd, self->tx_backlog, self->tx_used, MSG_NOSIGNAL);
        if (bytes_sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return true;
        }
        if (bytes_sent <= 0) {
            closeConnection(self, true);
            return false;
        }

        memmove(self->tx_backlog, self->tx_backlog + bytes_sent, self->tx_used - (size_t)bytes_sent);
        self->tx_used -= (size_t)bytes_sent;
    }

    return true;
}

static void dispatchMessages(TcpClientTransport *self)
{
    MessageHeader header;
    const uint8_t *message;
    size_t message_size;

    for (;;) {
        message_size = MessageFramer_NextMessage(&self->framer, &message);
        if (message_size == 0) {
            return;
        }

        MessageHeader_Decode(&header, message, message_size);
        if (header.type == kMESSAGE_TYPE_FRAME) {
            self->events.on_frame(self->events.context, message, message_size);
        } else {
            self->events.on_control(self->events.context, message, message_size, Clock_RealtimeUs());
        }
        MessageFramer_Consume(&self->framer, message_size);
    }
}

static void closeConnection(TcpClientTransport *self, bool notify)
{
    if (self->tcp_fd == NO_SOCKET) {
        return;
    }

    close(self->tcp_fd);
    self->tcp_fd = NO_SOCKET;
    self->connecting = false;
    self->connected = false;
    self->tx_used = 0;
    MessageFramer_Reset(&self->framer);

    if (notify) {
        self->events.on_disconnected(self->events.context, Clock_RealtimeUs());
    }
}
