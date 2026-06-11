#include "platform/windows/quic/quic_udp_endpoint.h"

#include <string.h>

/* ---------- public ---------- */

bool QuicUdpEndpoint_Open(QuicUdpEndpoint *self)
{
    SOCKET udp_socket;
    u_long nonblocking = 1;

    memset(self, 0, sizeof(*self));
    self->timer_expiry_ns = UINT64_MAX;

    udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_socket == INVALID_SOCKET) {
        return false;
    }
    ioctlsocket(udp_socket, FIONBIO, &nonblocking);
    self->udp_fd = (int32_t)udp_socket;

    return true;
}

bool QuicUdpEndpoint_ConnectTo(QuicUdpEndpoint *self, const char *host, const char *port)
{
    struct addrinfo hints;
    struct addrinfo *resolved;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, port, &hints, &resolved) != 0) {
        return false;
    }

    if (connect((SOCKET)self->udp_fd, resolved->ai_addr, (int)resolved->ai_addrlen) != 0) {
        freeaddrinfo(resolved);
        return false;
    }

    memcpy(&self->remote_address, resolved->ai_addr, resolved->ai_addrlen);
    self->remote_address_length = (socklen_t)resolved->ai_addrlen;
    freeaddrinfo(resolved);

    self->local_address_length = sizeof(self->local_address);
    getsockname((SOCKET)self->udp_fd, (struct sockaddr *)&self->local_address, &self->local_address_length);

    return true;
}

int QuicUdpEndpoint_Send(QuicUdpEndpoint *self, const uint8_t *data, size_t size)
{
    return send((SOCKET)self->udp_fd, (const char *)data, (int)size, 0);
}

int QuicUdpEndpoint_Receive(QuicUdpEndpoint *self, uint8_t *buffer, size_t buffer_size)
{
    return recv((SOCKET)self->udp_fd, (char *)buffer, (int)buffer_size, 0);
}

void QuicUdpEndpoint_MakePath(QuicUdpEndpoint *self, ngtcp2_path *path)
{
    path->local.addr = (ngtcp2_sockaddr *)&self->local_address;
    path->local.addrlen = self->local_address_length;
    path->remote.addr = (ngtcp2_sockaddr *)&self->remote_address;
    path->remote.addrlen = self->remote_address_length;
    path->user_data = NULL;
}

void QuicUdpEndpoint_ArmTimerAt(QuicUdpEndpoint *self, uint64_t expiry_ns)
{
    self->timer_expiry_ns = expiry_ns;
}

void QuicUdpEndpoint_DisarmTimer(QuicUdpEndpoint *self)
{
    self->timer_expiry_ns = UINT64_MAX;
}

void QuicUdpEndpoint_ClearTimerEvent(QuicUdpEndpoint *self)
{
    self->timer_expiry_ns = UINT64_MAX;
}

uint64_t QuicUdpEndpoint_TimerExpiryNs(const QuicUdpEndpoint *self)
{
    return self->timer_expiry_ns;
}
