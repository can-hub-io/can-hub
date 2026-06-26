#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <ngtcp2/ngtcp2.h>

#include <sys/socket.h>
#include <sys/types.h>

/*
 * UDP socket + ngtcp2 expiry timer for one QUIC connection. Both fds are
 * non-blocking. The timer fd is stable for the transport lifetime; the udp fd
 * is recreated on every reconnect (ReopenSocket) so it rebinds to the current
 * local address after the interface or IP changes, so the main loop must
 * re-register the live udp fd with epoll rather than treat it as static.
 */
typedef struct {
    int32_t udp_fd;
    int32_t timer_fd;
    struct sockaddr_storage local_address;
    socklen_t local_address_length;
    struct sockaddr_storage remote_address;
    socklen_t remote_address_length;
    bool gso_unsupported;
} QuicUdpEndpoint;

bool QuicUdpEndpoint_Open(QuicUdpEndpoint *self);
bool QuicUdpEndpoint_ReopenSocket(QuicUdpEndpoint *self);
bool QuicUdpEndpoint_ConnectTo(QuicUdpEndpoint *self, const char *host, const char *port);
ssize_t QuicUdpEndpoint_Send(QuicUdpEndpoint *self, const uint8_t *data, size_t size);
void QuicUdpEndpoint_SendSegmented(QuicUdpEndpoint *self, const uint8_t *data, size_t size, size_t segment_size);
ssize_t QuicUdpEndpoint_Receive(QuicUdpEndpoint *self, uint8_t *buffer, size_t buffer_size);
void QuicUdpEndpoint_MakePath(QuicUdpEndpoint *self, ngtcp2_path *path);
void QuicUdpEndpoint_ArmTimerAt(QuicUdpEndpoint *self, uint64_t expiry_ns);
void QuicUdpEndpoint_DisarmTimer(QuicUdpEndpoint *self);
void QuicUdpEndpoint_ClearTimerEvent(QuicUdpEndpoint *self);
