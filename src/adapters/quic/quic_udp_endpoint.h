#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <ngtcp2/ngtcp2.h>

#include <sys/socket.h>
#include <sys/types.h>

/*
 * UDP socket + ngtcp2 expiry timer for one QUIC connection. Both fds are
 * non-blocking and live for the whole transport lifetime (stable for epoll);
 * reconnects reuse them.
 */
typedef struct {
    int udp_fd;
    int timer_fd;
    struct sockaddr_storage local_address;
    socklen_t local_address_length;
    struct sockaddr_storage remote_address;
    socklen_t remote_address_length;
} QuicUdpEndpoint;

bool QuicUdpEndpoint_Open(QuicUdpEndpoint *self);
bool QuicUdpEndpoint_ConnectTo(QuicUdpEndpoint *self, const char *host, const char *port);
ssize_t QuicUdpEndpoint_Send(QuicUdpEndpoint *self, const uint8_t *data, size_t size);
ssize_t QuicUdpEndpoint_Receive(QuicUdpEndpoint *self, uint8_t *buffer, size_t buffer_size);
void QuicUdpEndpoint_MakePath(QuicUdpEndpoint *self, ngtcp2_path *path);
void QuicUdpEndpoint_ArmTimerAt(QuicUdpEndpoint *self, uint64_t expiry_ns);
void QuicUdpEndpoint_DisarmTimer(QuicUdpEndpoint *self);
void QuicUdpEndpoint_ClearTimerEvent(QuicUdpEndpoint *self);
