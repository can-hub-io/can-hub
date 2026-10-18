#include "platform/linux/shared/tcp_keepalive.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#define KEEPALIVE_IDLE_SECONDS 10
#define KEEPALIVE_INTERVAL_SECONDS 5
#define KEEPALIVE_PROBES 4
#define KEEPALIVE_MAX_RETRANSMIT_SECONDS 30

void TcpKeepalive_Enable(int32_t fd)
{
    SOCKET tcp_socket = (SOCKET)fd;
    int32_t enabled = 1;
    uint32_t idle_seconds = KEEPALIVE_IDLE_SECONDS;
    uint32_t interval_seconds = KEEPALIVE_INTERVAL_SECONDS;
    uint32_t probes = KEEPALIVE_PROBES;
    uint32_t max_retransmit_seconds = KEEPALIVE_MAX_RETRANSMIT_SECONDS;

    setsockopt(tcp_socket, SOL_SOCKET, SO_KEEPALIVE, (const char *)&enabled, sizeof(enabled));
    setsockopt(tcp_socket, IPPROTO_TCP, TCP_KEEPIDLE, (const char *)&idle_seconds, sizeof(idle_seconds));
    setsockopt(tcp_socket, IPPROTO_TCP, TCP_KEEPINTVL, (const char *)&interval_seconds, sizeof(interval_seconds));
    setsockopt(tcp_socket, IPPROTO_TCP, TCP_KEEPCNT, (const char *)&probes, sizeof(probes));
    setsockopt(tcp_socket, IPPROTO_TCP, TCP_MAXRT, (const char *)&max_retransmit_seconds, sizeof(max_retransmit_seconds));
}
