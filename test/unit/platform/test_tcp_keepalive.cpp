#include <cest>

extern "C" {
#include "platform/linux/shared/tcp_keepalive.h"
}

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

static int32_t keepalive;
static uint32_t user_timeout_ms;

static void enableKeepaliveOnFreshSocket();

describe("tcp_keepalive_enable", []() {
    it("bounds how long a silent link can hold its socket", []() {
        enableKeepaliveOnFreshSocket();

        expect(keepalive).toBe(1);
        expect(user_timeout_ms > 0).toBe(true);
    });
});

static void enableKeepaliveOnFreshSocket()
{
    int32_t fd = socket(AF_INET, SOCK_STREAM, 0);
    socklen_t keepalive_size = sizeof(keepalive);
    socklen_t user_timeout_size = sizeof(user_timeout_ms);

    keepalive = 0;
    user_timeout_ms = 0;
    TcpKeepalive_Enable(fd);
    getsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, &keepalive_size);
    getsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &user_timeout_ms, &user_timeout_size);
    close(fd);
}
