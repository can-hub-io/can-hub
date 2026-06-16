#include "platform/linux/tcp/tcp_client_transport.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "platform/linux/clock/clock.h"
#include "protocol/message_header.h"

static bool portConnect(void *context);
static void portDisconnect(void *context);
static bool portSendControl(void *context, const uint8_t *data, size_t size);
static bool portSendFrame(void *context, const uint8_t *data, size_t size);
static void initTransportBase(TcpClientTransport *self, const TransportEvents *events);
static bool connectTcp(TcpClientTransport *self, int32_t *connected_fd);
static bool connectUnix(TcpClientTransport *self, int32_t *connected_fd);
static void dispatchMessage(void *context, const uint8_t *message, size_t size);
static void closeConnection(TcpClientTransport *self, bool notify);

/* ---------- public ---------- */

bool TcpClientTransport_Init(
    TcpClientTransport *self,
    const char *host,
    const char *port,
    const TransportEvents *events
)
{
    initTransportBase(self, events);
    snprintf(self->host, TCP_HOST_MAX, "%s", host);
    snprintf(self->port_text, TCP_PORT_TEXT_MAX, "%s", port);

    return true;
}

bool TcpClientTransport_InitUnix(TcpClientTransport *self, const char *socket_path, const TransportEvents *events)
{
    struct sockaddr_un address;

    if (strlen(socket_path) >= sizeof(address.sun_path)) {
        return false;
    }

    initTransportBase(self, events);
    self->unix_socket = true;
    snprintf(self->host, TCP_HOST_MAX, "%s", socket_path);

    return true;
}

TransportPort *TcpClientTransport_Port(TcpClientTransport *self)
{
    return &self->port;
}

int32_t TcpClientTransport_Fd(const TcpClientTransport *self)
{
    return self->channel.fd;
}

bool TcpClientTransport_WantsWritable(const TcpClientTransport *self)
{
    return self->connecting || TcpChannel_HasPendingTx(&self->channel);
}

void TcpClientTransport_OnReadable(TcpClientTransport *self)
{
    MessageSink sink = { self, dispatchMessage };

    if (!TcpChannel_IsBound(&self->channel)) {
        return;
    }

    if (!TcpChannel_Receive(&self->channel, &sink)) {
        closeConnection(self, true);
    }
}

void TcpClientTransport_OnWritable(TcpClientTransport *self)
{
    int32_t socket_error = 0;
    socklen_t error_length = sizeof(socket_error);

    if (!TcpChannel_IsBound(&self->channel)) {
        return;
    }

    if (self->connecting) {
        getsockopt(self->channel.fd, SOL_SOCKET, SO_ERROR, &socket_error, &error_length);
        if (socket_error != 0) {
            closeConnection(self, true);
            return;
        }

        self->connecting = false;
        self->connected = true;
        self->events.on_connected(self->events.context);
        return;
    }

    if (!TcpChannel_Flush(&self->channel)) {
        closeConnection(self, true);
    }
}

/* ---------- private: transport port ---------- */

static bool portConnect(void *context)
{
    TcpClientTransport *self = context;
    int32_t connected_fd;
    bool connection_started;

    if (TcpChannel_IsBound(&self->channel)) {
        return true;
    }

    if (self->unix_socket) {
        connection_started = connectUnix(self, &connected_fd);
    } else {
        connection_started = connectTcp(self, &connected_fd);
    }
    if (!connection_started) {
        return false;
    }

    TcpChannel_Bind(&self->channel, connected_fd);
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
    if (!TcpChannel_Queue(&self->channel, data, size)) {
        return false;
    }
    if (!TcpChannel_Flush(&self->channel)) {
        closeConnection(self, true);
        return false;
    }

    return true;
}

static bool portSendFrame(void *context, const uint8_t *data, size_t size)
{
    TcpClientTransport *self = context;

    if (!self->connected) {
        return false;
    }
    if (TcpChannel_FreeTxSpace(&self->channel) < size) {
        return false;
    }

    return portSendControl(context, data, size);
}

/* ---------- private ---------- */

static void initTransportBase(TcpClientTransport *self, const TransportEvents *events)
{
    memset(self, 0, sizeof(*self));
    self->port.context = self;
    self->port.connect = portConnect;
    self->port.disconnect = portDisconnect;
    self->port.send_control = portSendControl;
    self->port.send_frame = portSendFrame;
    self->events = *events;
    TcpChannel_Unbind(&self->channel);
}

static bool connectTcp(TcpClientTransport *self, int32_t *connected_fd)
{
    struct addrinfo hints;
    struct addrinfo *resolved;
    int32_t tcp_fd;
    int32_t connect_result;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(self->host, self->port_text, &hints, &resolved) != 0) {
        return false;
    }

    tcp_fd = socket(resolved->ai_family, resolved->ai_socktype | SOCK_NONBLOCK, 0);
    if (tcp_fd < 0) {
        freeaddrinfo(resolved);
        return false;
    }

    connect_result = connect(tcp_fd, resolved->ai_addr, resolved->ai_addrlen);
    freeaddrinfo(resolved);
    if (connect_result < 0 && errno != EINPROGRESS) {
        close(tcp_fd);
        return false;
    }

    *connected_fd = tcp_fd;

    return true;
}

static bool connectUnix(TcpClientTransport *self, int32_t *connected_fd)
{
    struct sockaddr_un address;
    size_t path_length = strlen(self->host);
    int32_t unix_fd;
    int32_t connect_result;

    if (path_length >= sizeof(address.sun_path)) {
        return false;
    }

    unix_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (unix_fd < 0) {
        return false;
    }

    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, self->host, path_length);
    connect_result = connect(unix_fd, (struct sockaddr *)&address, sizeof(address));
    if (connect_result < 0 && errno != EINPROGRESS && errno != EAGAIN) {
        close(unix_fd);
        return false;
    }

    *connected_fd = unix_fd;

    return true;
}

static void dispatchMessage(void *context, const uint8_t *message, size_t size)
{
    TcpClientTransport *self = context;
    MessageHeader header;

    MessageHeader_Decode(&header, message, size);
    if (header.type == kMESSAGE_TYPE_FRAME) {
        self->events.on_frame(self->events.context, message, size);
    } else {
        self->events.on_control(self->events.context, message, size, Clock_RealtimeUs());
    }
}

static void closeConnection(TcpClientTransport *self, bool notify)
{
    if (!TcpChannel_IsBound(&self->channel)) {
        return;
    }

    close(self->channel.fd);
    TcpChannel_Unbind(&self->channel);
    self->connecting = false;
    self->connected = false;

    if (notify) {
        self->events.on_disconnected(self->events.context, Clock_RealtimeUs());
    }
}
