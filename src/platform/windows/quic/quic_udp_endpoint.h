#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <ngtcp2/ngtcp2.h>

#include <winsock2.h>
#include <ws2tcpip.h>

/*
 * UDP socket + ngtcp2 expiry deadline for one QUIC connection over winsock.
 * Windows has no timerfd: the armed expiry is exposed as a deadline
 * (TimerExpiryNs) and the platform pump turns it into its poll timeout,
 * calling the transport timer handler once the deadline passes.
 */
typedef struct {
    int32_t udp_fd;
    uint64_t timer_expiry_ns;
    struct sockaddr_storage local_address;
    socklen_t local_address_length;
    struct sockaddr_storage remote_address;
    socklen_t remote_address_length;
} QuicUdpEndpoint;

bool QuicUdpEndpoint_Open(QuicUdpEndpoint *self);
bool QuicUdpEndpoint_ConnectTo(QuicUdpEndpoint *self, const char *host, const char *port);
int QuicUdpEndpoint_Send(QuicUdpEndpoint *self, const uint8_t *data, size_t size);
int QuicUdpEndpoint_Receive(QuicUdpEndpoint *self, uint8_t *buffer, size_t buffer_size);
void QuicUdpEndpoint_MakePath(QuicUdpEndpoint *self, ngtcp2_path *path);
void QuicUdpEndpoint_ArmTimerAt(QuicUdpEndpoint *self, uint64_t expiry_ns);
void QuicUdpEndpoint_DisarmTimer(QuicUdpEndpoint *self);
void QuicUdpEndpoint_ClearTimerEvent(QuicUdpEndpoint *self);
uint64_t QuicUdpEndpoint_TimerExpiryNs(const QuicUdpEndpoint *self);
