#include "platform/linux/shared/tcp_keepalive.h"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#define KEEPALIVE_IDLE_SECONDS 10
#define KEEPALIVE_INTERVAL_SECONDS 5
#define KEEPALIVE_PROBES 4
#define KEEPALIVE_USER_TIMEOUT_MS 30000

void TcpKeepalive_Enable(int32_t fd)
{
    int32_t enabled = 1;
    int32_t idle_seconds = KEEPALIVE_IDLE_SECONDS;
    int32_t interval_seconds = KEEPALIVE_INTERVAL_SECONDS;
    int32_t probes = KEEPALIVE_PROBES;
    uint32_t user_timeout_ms = KEEPALIVE_USER_TIMEOUT_MS;

    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &enabled, sizeof(enabled));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle_seconds, sizeof(idle_seconds));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval_seconds, sizeof(interval_seconds));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &probes, sizeof(probes));
    setsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &user_timeout_ms, sizeof(user_timeout_ms));
}
