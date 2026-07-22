#include "platform/linux/quic/quic_udp_endpoint.h"

#include <string.h>
#include <unistd.h>

#include "platform/linux/quic/quic_udp_gso.h"

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/timerfd.h>

#define UDP_SOCKET_BUFFER_BYTES (4 * 1024 * 1024)

static int32_t openSocket(int32_t *address_family);

/* ---------- public ---------- */

bool QuicUdpEndpoint_Open(QuicUdpEndpoint *self)
{
    memset(self, 0, sizeof(*self));
    self->udp_fd = openSocket(&self->address_family);
    self->timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);

    return self->udp_fd >= 0 && self->timer_fd >= 0;
}

bool QuicUdpEndpoint_ReopenSocket(QuicUdpEndpoint *self)
{
    int32_t previous_fd = self->udp_fd;

    self->udp_fd = openSocket(&self->address_family);
    if (previous_fd >= 0) {
        close(previous_fd);
    }
    self->gso_unsupported = false;
    self->local_address_length = 0;
    self->remote_address_length = 0;

    return self->udp_fd >= 0;
}

bool QuicUdpEndpoint_ConnectTo(QuicUdpEndpoint *self, const char *host, const char *port)
{
    struct addrinfo hints;
    struct addrinfo *resolved;
    struct addrinfo *candidate;
    bool connected;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = self->address_family;
    hints.ai_socktype = SOCK_DGRAM;
    if (self->address_family == AF_INET6) {
        hints.ai_flags = AI_V4MAPPED | AI_ALL;
    }
    if (getaddrinfo(host, port, &hints, &resolved) != 0) {
        return false;
    }

    connected = false;
    for(candidate=resolved; candidate!=NULL; candidate=candidate->ai_next) {
        if (connect(self->udp_fd, candidate->ai_addr, candidate->ai_addrlen) < 0) {
            continue;
        }
        memcpy(&self->remote_address, candidate->ai_addr, candidate->ai_addrlen);
        self->remote_address_length = candidate->ai_addrlen;
        connected = true;
        break;
    }
    freeaddrinfo(resolved);
    if (!connected) {
        return false;
    }

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

/* ---------- private ---------- */

static int32_t openSocket(int32_t *address_family)
{
    int32_t buffer_bytes = UDP_SOCKET_BUFFER_BYTES;
    int32_t dual_stack = 0;
    int32_t fd;

    *address_family = AF_INET6;
    fd = socket(AF_INET6, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        *address_family = AF_INET;
        fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    }
    if (fd < 0) {
        return fd;
    }
    if (*address_family == AF_INET6) {
        setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &dual_stack, sizeof(dual_stack));
    }

    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buffer_bytes, sizeof(buffer_bytes));
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buffer_bytes, sizeof(buffer_bytes));

    return fd;
}
