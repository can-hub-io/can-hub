#include "platform/linux/quic/quic_udp_endpoint.h"

#include <string.h>
#include <unistd.h>

#include "platform/linux/quic/quic_udp_gso.h"

#include <netdb.h>
#include <sys/socket.h>
#include <sys/timerfd.h>

#define UDP_SOCKET_BUFFER_BYTES (4 * 1024 * 1024)

/* ---------- public ---------- */

bool QuicUdpEndpoint_Open(QuicUdpEndpoint *self)
{
    int32_t buffer_bytes = UDP_SOCKET_BUFFER_BYTES;

    memset(self, 0, sizeof(*self));
    self->udp_fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    self->timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);

    if (self->udp_fd >= 0) {
        setsockopt(self->udp_fd, SOL_SOCKET, SO_RCVBUF, &buffer_bytes, sizeof(buffer_bytes));
        setsockopt(self->udp_fd, SOL_SOCKET, SO_SNDBUF, &buffer_bytes, sizeof(buffer_bytes));
    }

    return self->udp_fd >= 0 && self->timer_fd >= 0;
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

    if (connect(self->udp_fd, resolved->ai_addr, resolved->ai_addrlen) < 0) {
        freeaddrinfo(resolved);
        return false;
    }

    memcpy(&self->remote_address, resolved->ai_addr, resolved->ai_addrlen);
    self->remote_address_length = resolved->ai_addrlen;
    freeaddrinfo(resolved);

    self->local_address_length = sizeof(self->local_address);
    getsockname(self->udp_fd, (struct sockaddr *)&self->local_address, &self->local_address_length);

    return true;
}

ssize_t QuicUdpEndpoint_Send(QuicUdpEndpoint *self, const uint8_t *data, size_t size)
{
    return send(self->udp_fd, data, size, 0);
}

void QuicUdpEndpoint_SendSegmented(QuicUdpEndpoint *self, const uint8_t *data, size_t size, size_t segment_size)
{
    QuicUdpGso_Send(self->udp_fd, NULL, 0, data, size, segment_size, &self->gso_unsupported);
}

ssize_t QuicUdpEndpoint_Receive(QuicUdpEndpoint *self, uint8_t *buffer, size_t buffer_size)
{
    return recv(self->udp_fd, buffer, buffer_size, 0);
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
    struct itimerspec timer_specification;

    memset(&timer_specification, 0, sizeof(timer_specification));
    timer_specification.it_value.tv_sec = (time_t)(expiry_ns / NGTCP2_SECONDS);
    timer_specification.it_value.tv_nsec = (long)(expiry_ns % NGTCP2_SECONDS);
    timerfd_settime(self->timer_fd, TFD_TIMER_ABSTIME, &timer_specification, NULL);
}

void QuicUdpEndpoint_DisarmTimer(QuicUdpEndpoint *self)
{
    struct itimerspec timer_specification;

    memset(&timer_specification, 0, sizeof(timer_specification));
    timerfd_settime(self->timer_fd, 0, &timer_specification, NULL);
}

void QuicUdpEndpoint_ClearTimerEvent(QuicUdpEndpoint *self)
{
    uint64_t expiration_count;
    ssize_t bytes_read;

    bytes_read = read(self->timer_fd, &expiration_count, sizeof(expiration_count));
    (void)bytes_read;
}
