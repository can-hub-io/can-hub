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

static bool portConnect(void *context);
static void portDisconnect(void *context);
static bool portSendControl(void *context, const uint8_t *data, size_t size);
static bool portSendFrame(void *context, const uint8_t *data, size_t size);
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
    TcpChannel_Unbind(&self->channel);
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
    return self->channel.fd;
}

bool TcpClientTransport_WantsWritable(const TcpClientTransport *self)
{
    return self->connecting || TcpChannel_HasPendingTx(&self->channel);
}

void TcpClientTransport_OnReadable(TcpClientTransport *self)
{
    if (!TcpChannel_IsBound(&self->channel)) {
        return;
    }

    if (!TcpChannel_Receive(&self->channel)) {
        closeConnection(self, true);
        return;
    }

    dispatchMessages(self);
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
    struct addrinfo hints;
    struct addrinfo *resolved;
    int32_t tcp_fd;
    int32_t connect_result;

    if (TcpChannel_IsBound(&self->channel)) {
        return true;
    }

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

    TcpChannel_Bind(&self->channel, tcp_fd);
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

static void dispatchMessages(TcpClientTransport *self)
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
