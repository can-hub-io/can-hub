#include "platform/linux/quic/quic_client_transport.h"

#include <stdio.h>
#include <string.h>

#include "platform/linux/clock/clock.h"
#include "platform/linux/quic/quic_egress.h"

#define UDP_PACKET_BUFFER_SIZE 1452

static bool portConnect(void *context);
static void portDisconnect(void *context);
static bool portSendControl(void *context, const uint8_t *data, size_t size);
static bool portSendFrame(void *context, const uint8_t *data, size_t size);
static void teardown(QuicClientTransport *self, bool notify);
static bool flushEgress(QuicClientTransport *self, const uint8_t *datagram, size_t datagram_size);
static void sendPacket(void *context, const uint8_t *packet, size_t size);
static void rearmTimer(QuicClientTransport *self);
static void dispatchControlMessages(QuicClientTransport *self);
static void onHandshakeCompleted(void *context);
static void onDatagram(void *context, const uint8_t *data, size_t size);
static void onStreamData(void *context, int64_t stream_id, const uint8_t *data, size_t size);
static void onStreamAcked(void *context, int64_t stream_id, uint64_t acked_end_offset);

/* ---------- public ---------- */

bool QuicClientTransport_Init(
    QuicClientTransport *self,
    const char *host,
    const char *port,
    const TransportEvents *events,
    const QuicClientSecurityConfig *security_config
)
{
    QuicConnectionEvents connection_events = {
        .context = self,
        .on_handshake_completed = onHandshakeCompleted,
        .on_datagram = onDatagram,
        .on_stream_data = onStreamData,
        .on_stream_acked = onStreamAcked,
    };

    memset(self, 0, sizeof(*self));
    self->port.context = self;
    self->port.connect = portConnect;
    self->port.disconnect = portDisconnect;
    self->port.send_control = portSendControl;
    self->port.send_frame = portSendFrame;
    self->events = *events;
    if (security_config != NULL) {
        self->security_config = *security_config;
    }
    QuicConnection_Bind(&self->connection, &connection_events);
    QuicControlChannel_Reset(&self->control);
    snprintf(self->server.host, QUIC_HOST_MAX, "%s", host);
    snprintf(self->server.port_text, QUIC_PORT_TEXT_MAX, "%s", port);

    return QuicUdpEndpoint_Open(&self->udp);
}

TransportPort *QuicClientTransport_Port(QuicClientTransport *self)
{
    return &self->port;
}

int32_t QuicClientTransport_UdpFd(const QuicClientTransport *self)
{
    return self->udp.udp_fd;
}

int32_t QuicClientTransport_TimerFd(const QuicClientTransport *self)
{
    return self->udp.timer_fd;
}

void QuicClientTransport_OnUdpReadable(QuicClientTransport *self)
{
    uint8_t buffer[UDP_PACKET_BUFFER_SIZE];
    ngtcp2_path path;
    ssize_t bytes_received;

    if (!QuicConnection_IsOpen(&self->connection)) {
        return;
    }

    for (;;) {
        bytes_received = QuicUdpEndpoint_Receive(&self->udp, buffer, sizeof(buffer));
        if (bytes_received <= 0) {
            break;
        }

        QuicUdpEndpoint_MakePath(&self->udp, &path);
        self->dispatching = true;
        if (!QuicConnection_ReadPacket(&self->connection, &path, buffer, (size_t)bytes_received)) {
            self->dispatching = false;
            teardown(self, true);
            return;
        }
        self->dispatching = false;

        if (self->disconnect_pending) {
            flushEgress(self, NULL, 0);
            teardown(self, false);
            return;
        }
    }

    flushEgress(self, NULL, 0);
}

void QuicClientTransport_OnTimer(QuicClientTransport *self)
{
    QuicUdpEndpoint_ClearTimerEvent(&self->udp);
    if (!QuicConnection_IsOpen(&self->connection)) {
        return;
    }

    self->dispatching = true;
    if (!QuicConnection_HandleExpiry(&self->connection)) {
        self->dispatching = false;
        teardown(self, true);
        return;
    }
    self->dispatching = false;

    if (self->disconnect_pending) {
        flushEgress(self, NULL, 0);
        teardown(self, false);
        return;
    }

    flushEgress(self, NULL, 0);
}

/* ---------- private: transport port ---------- */

static bool portConnect(void *context)
{
    QuicClientTransport *self = context;
    ngtcp2_path path;

    if (QuicConnection_IsOpen(&self->connection)) {
        return true;
    }
    if (!QuicUdpEndpoint_ConnectTo(&self->udp, self->server.host, self->server.port_text)) {
        return false;
    }
    if (!QuicClientSecurity_Init(&self->security, self->server.host, QuicConnection_Ref(&self->connection), &self->security_config)) {
        return false;
    }

    QuicUdpEndpoint_MakePath(&self->udp, &path);
    if (!QuicConnection_Open(&self->connection, self->security.session, &path)) {
        QuicClientSecurity_Free(&self->security);
        return false;
    }

    return flushEgress(self, NULL, 0);
}

static void portDisconnect(void *context)
{
    QuicClientTransport *self = context;
    uint8_t packet[UDP_PACKET_BUFFER_SIZE];
    ngtcp2_ssize written;

    if (self->dispatching) {
        self->disconnect_pending = true;
        return;
    }

    if (self->connected && QuicConnection_IsOpen(&self->connection)) {
        written = QuicConnection_WriteConnectionClose(&self->connection, packet, sizeof(packet));
        if (written > 0) {
            QuicUdpEndpoint_Send(&self->udp, packet, (size_t)written);
        }
    }

    teardown(self, false);
}

static bool portSendControl(void *context, const uint8_t *data, size_t size)
{
    QuicClientTransport *self = context;

    if (!self->connected || self->disconnect_pending) {
        return false;
    }
    if (!QuicControlChannel_QueueTx(&self->control, data, size)) {
        return false;
    }
    if (self->dispatching) {
        return true;
    }

    return flushEgress(self, NULL, 0);
}

static bool portSendFrame(void *context, const uint8_t *data, size_t size)
{
    QuicClientTransport *self = context;

    if (!self->connected) {
        return false;
    }

    return flushEgress(self, data, size);
}

/* ---------- private: lifecycle ---------- */

static void teardown(QuicClientTransport *self, bool notify)
{
    if (!QuicConnection_IsOpen(&self->connection)) {
        return;
    }

    QuicConnection_Close(&self->connection);
    QuicClientSecurity_Free(&self->security);
    QuicControlChannel_Reset(&self->control);
    QuicUdpEndpoint_DisarmTimer(&self->udp);
    self->connected = false;
    self->disconnect_pending = false;

    if (notify) {
        self->events.on_disconnected(self->events.context, Clock_RealtimeUs());
    }
}

/* ---------- private: egress pump ---------- */

static bool flushEgress(QuicClientTransport *self, const uint8_t *datagram, size_t datagram_size)
{
    QuicEgressSink sink = { self, sendPacket };
    bool datagram_accepted = true;

    if (!QuicConnection_IsOpen(&self->connection)) {
        return false;
    }

    if (datagram != NULL) {
        if (!QuicEgress_FlushDatagram(&self->connection, &sink, datagram, datagram_size, &datagram_accepted)) {
            teardown(self, true);
            return false;
        }
    }
    if (!QuicEgress_Drain(&self->connection, &self->control, &sink)) {
        teardown(self, true);
        return false;
    }

    rearmTimer(self);

    return datagram_accepted;
}

static void sendPacket(void *context, const uint8_t *packet, size_t size)
{
    QuicClientTransport *self = context;

    QuicUdpEndpoint_Send(&self->udp, packet, size);
}

static void rearmTimer(QuicClientTransport *self)
{
    uint64_t expiry_ns = QuicConnection_NextExpiryNs(&self->connection);

    if (expiry_ns == UINT64_MAX) {
        QuicUdpEndpoint_DisarmTimer(&self->udp);
        return;
    }

    QuicUdpEndpoint_ArmTimerAt(&self->udp, expiry_ns);
}

/* ---------- private: connection events ---------- */

static void dispatchControlMessages(QuicClientTransport *self)
{
    const uint8_t *message;
    size_t message_size;

    for (;;) {
        message_size = QuicControlChannel_NextMessage(&self->control, &message);
        if (message_size == 0) {
            return;
        }

        self->events.on_control(self->events.context, message, message_size, Clock_RealtimeUs());
        QuicControlChannel_ConsumeMessage(&self->control, message_size);
    }
}

static void onHandshakeCompleted(void *context)
{
    QuicClientTransport *self = context;

    if (!QuicConnection_OpenControlStream(&self->connection, &self->control.stream_id)) {
        return;
    }

    self->connected = true;
    self->events.on_connected(self->events.context);
}

static void onDatagram(void *context, const uint8_t *data, size_t size)
{
    QuicClientTransport *self = context;

    self->events.on_frame(self->events.context, data, size);
}

static void onStreamData(void *context, int64_t stream_id, const uint8_t *data, size_t size)
{
    QuicClientTransport *self = context;

    if (stream_id != self->control.stream_id) {
        return;
    }
    if (!QuicControlChannel_QueueRx(&self->control, data, size)) {
        return;
    }

    dispatchControlMessages(self);
    QuicConnection_ExtendStreamCredit(&self->connection, stream_id, size);
}

static void onStreamAcked(void *context, int64_t stream_id, uint64_t acked_end_offset)
{
    QuicClientTransport *self = context;

    if (stream_id != self->control.stream_id) {
        return;
    }

    QuicControlChannel_MarkAcked(&self->control, acked_end_offset);
}
