/*
 * E0 spike baseline — CAN over plain TCP (TCP_NODELAY) or plain UDP.
 *
 * Same bridging semantics as tunnel.c (one struct can_frame per message,
 * raw memory layout, no encryption) to compare transport overhead against
 * the QUIC datagram tunnel.
 *
 *   ./plain_tunnel tcp-server <port> <can-if>
 *   ./plain_tunnel tcp-client <host> <port> <can-if>
 *   ./plain_tunnel udp-server <port> <can-if>
 *   ./plain_tunnel udp-client <host> <port> <can-if>
 */

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#define MAX_EPOLL_EVENTS 8
#define FRAME_SIZE sizeof(struct can_frame)

typedef struct {
    bool is_tcp;
    bool is_server;
    bool peer_known;
    int network_fd;
    int can_fd;
    int epoll_fd;
    struct sockaddr_storage peer_address;
    socklen_t peer_address_length;
    uint8_t stream_buffer[FRAME_SIZE];
    size_t stream_buffer_used;
    uint64_t tx_count;
    uint64_t rx_count;
} PlainTunnel;

static PlainTunnel tunnel;

static int openCanSocket(const char *interface_name);
static int openTcpServer(PlainTunnel *self, const char *port);
static int openTcpClient(PlainTunnel *self, const char *host, const char *port);
static int openUdpServer(PlainTunnel *self, const char *port);
static int openUdpClient(PlainTunnel *self, const char *host, const char *port);
static int handleNetworkInput(PlainTunnel *self);
static int handleCanInput(PlainTunnel *self);
static void printStatsAndExit(int signal_number);

int main(int argc, char **argv)
{
    struct epoll_event events[MAX_EPOLL_EVENTS];
    struct epoll_event event;
    const char *mode;
    const char *can_interface;
    int event_count;
    int event_fd;
    int result;
    int i;

    if (argc < 4) {
        fprintf(stderr, "usage: %s tcp-server|udp-server <port> <can-if>\n", argv[0]);
        fprintf(stderr, "       %s tcp-client|udp-client <host> <port> <can-if>\n", argv[0]);
        return 1;
    }
    mode = argv[1];

    if (strcmp(mode, "tcp-server") == 0 && argc == 4) {
        tunnel.is_tcp = true;
        tunnel.is_server = true;
        can_interface = argv[3];
        result = openTcpServer(&tunnel, argv[2]);
    } else if (strcmp(mode, "tcp-client") == 0 && argc == 5) {
        tunnel.is_tcp = true;
        can_interface = argv[4];
        result = openTcpClient(&tunnel, argv[2], argv[3]);
    } else if (strcmp(mode, "udp-server") == 0 && argc == 4) {
        tunnel.is_server = true;
        can_interface = argv[3];
        result = openUdpServer(&tunnel, argv[2]);
    } else if (strcmp(mode, "udp-client") == 0 && argc == 5) {
        can_interface = argv[4];
        result = openUdpClient(&tunnel, argv[2], argv[3]);
    } else {
        fprintf(stderr, "unknown mode or wrong argument count\n");
        return 1;
    }
    if (result != 0) {
        return 1;
    }

    tunnel.can_fd = openCanSocket(can_interface);
    if (tunnel.can_fd < 0) {
        return 1;
    }

    tunnel.epoll_fd = epoll_create1(0);
    if (tunnel.epoll_fd < 0) {
        fprintf(stderr, "epoll: %s\n", strerror(errno));
        return 1;
    }
    event.events = EPOLLIN;
    event.data.fd = tunnel.network_fd;
    epoll_ctl(tunnel.epoll_fd, EPOLL_CTL_ADD, tunnel.network_fd, &event);
    event.data.fd = tunnel.can_fd;
    epoll_ctl(tunnel.epoll_fd, EPOLL_CTL_ADD, tunnel.can_fd, &event);

    signal(SIGINT, printStatsAndExit);
    signal(SIGTERM, printStatsAndExit);
    fprintf(stderr, "[%s] ready\n", mode);

    for (;;) {
        event_count = epoll_wait(tunnel.epoll_fd, events, MAX_EPOLL_EVENTS, -1);
        if (event_count < 0 && errno == EINTR) {
            continue;
        }
        for(i=0; i<event_count; i++) {
            event_fd = events[i].data.fd;
            result = 0;

            if (event_fd == tunnel.network_fd) {
                result = handleNetworkInput(&tunnel);
            } else if (event_fd == tunnel.can_fd) {
                result = handleCanInput(&tunnel);
            }
            if (result != 0) {
                fprintf(stderr, "fatal event loop error\n");
                return 1;
            }
        }
    }
}

static int openCanSocket(const char *interface_name)
{
    struct sockaddr_can address;
    struct ifreq interface_request;
    int can_fd;

    can_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (can_fd < 0) {
        fprintf(stderr, "CAN socket: %s\n", strerror(errno));
        return -1;
    }

    snprintf(interface_request.ifr_name, IFNAMSIZ, "%s", interface_name);
    if (ioctl(can_fd, SIOCGIFINDEX, &interface_request) < 0) {
        fprintf(stderr, "SIOCGIFINDEX %s: %s\n", interface_name, strerror(errno));
        return -1;
    }

    memset(&address, 0, sizeof(address));
    address.can_family = AF_CAN;
    address.can_ifindex = interface_request.ifr_ifindex;
    if (bind(can_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        fprintf(stderr, "CAN bind: %s\n", strerror(errno));
        return -1;
    }

    return can_fd;
}

static int openTcpServer(PlainTunnel *self, const char *port)
{
    struct sockaddr_in address;
    int listen_fd;
    int enable = 1;

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        fprintf(stderr, "TCP socket: %s\n", strerror(errno));
        return -1;
    }
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((uint16_t)atoi(port));
    if (bind(listen_fd, (struct sockaddr *)&address, sizeof(address)) < 0 || listen(listen_fd, 1) < 0) {
        fprintf(stderr, "TCP bind/listen: %s\n", strerror(errno));
        return -1;
    }

    self->network_fd = accept(listen_fd, NULL, NULL);
    if (self->network_fd < 0) {
        fprintf(stderr, "TCP accept: %s\n", strerror(errno));
        return -1;
    }
    close(listen_fd);
    setsockopt(self->network_fd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable));
    self->peer_known = true;

    return 0;
}

static int openTcpClient(PlainTunnel *self, const char *host, const char *port)
{
    struct addrinfo hints;
    struct addrinfo *resolved;
    int enable = 1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &resolved) != 0) {
        fprintf(stderr, "getaddrinfo failed\n");
        return -1;
    }

    self->network_fd = socket(resolved->ai_family, resolved->ai_socktype, 0);
    if (self->network_fd < 0 || connect(self->network_fd, resolved->ai_addr, resolved->ai_addrlen) < 0) {
        fprintf(stderr, "TCP connect: %s\n", strerror(errno));
        return -1;
    }
    freeaddrinfo(resolved);
    setsockopt(self->network_fd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable));
    self->peer_known = true;

    return 0;
}

static int openUdpServer(PlainTunnel *self, const char *port)
{
    struct sockaddr_in address;

    self->network_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (self->network_fd < 0) {
        fprintf(stderr, "UDP socket: %s\n", strerror(errno));
        return -1;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((uint16_t)atoi(port));
    if (bind(self->network_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        fprintf(stderr, "UDP bind: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

static int openUdpClient(PlainTunnel *self, const char *host, const char *port)
{
    struct addrinfo hints;
    struct addrinfo *resolved;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, port, &hints, &resolved) != 0) {
        fprintf(stderr, "getaddrinfo failed\n");
        return -1;
    }

    self->network_fd = socket(resolved->ai_family, resolved->ai_socktype, 0);
    if (self->network_fd < 0 || connect(self->network_fd, resolved->ai_addr, resolved->ai_addrlen) < 0) {
        fprintf(stderr, "UDP connect: %s\n", strerror(errno));
        return -1;
    }
    freeaddrinfo(resolved);
    self->peer_known = true;

    return 0;
}

static int handleNetworkInput(PlainTunnel *self)
{
    uint8_t buffer[FRAME_SIZE];
    ssize_t bytes_received;
    size_t missing;

    if (self->is_tcp) {
        missing = FRAME_SIZE - self->stream_buffer_used;
        bytes_received = recv(self->network_fd, self->stream_buffer + self->stream_buffer_used, missing, 0);
        if (bytes_received <= 0) {
            fprintf(stderr, "TCP peer closed\n");
            return -1;
        }
        self->stream_buffer_used += (size_t)bytes_received;
        if (self->stream_buffer_used < FRAME_SIZE) {
            return 0;
        }
        self->stream_buffer_used = 0;
        if (write(self->can_fd, self->stream_buffer, FRAME_SIZE) != (ssize_t)FRAME_SIZE) {
            fprintf(stderr, "CAN write: %s\n", strerror(errno));
            return -1;
        }
        self->rx_count++;
        return 0;
    }

    self->peer_address_length = sizeof(self->peer_address);
    bytes_received = recvfrom(
        self->network_fd,
        buffer,
        sizeof(buffer),
        0,
        (struct sockaddr *)&self->peer_address,
        &self->peer_address_length
    );
    if (bytes_received != (ssize_t)FRAME_SIZE) {
        return 0;
    }
    if (self->is_server && !self->peer_known) {
        connect(self->network_fd, (struct sockaddr *)&self->peer_address, self->peer_address_length);
        self->peer_known = true;
    }
    if (write(self->can_fd, buffer, FRAME_SIZE) != (ssize_t)FRAME_SIZE) {
        fprintf(stderr, "CAN write: %s\n", strerror(errno));
        return -1;
    }
    self->rx_count++;

    return 0;
}

static int handleCanInput(PlainTunnel *self)
{
    struct can_frame frame;
    ssize_t bytes_received;

    bytes_received = read(self->can_fd, &frame, sizeof(frame));
    if (bytes_received != (ssize_t)sizeof(frame)) {
        return 0;
    }
    if (!self->peer_known) {
        return 0;
    }
    if (send(self->network_fd, &frame, sizeof(frame), 0) == (ssize_t)sizeof(frame)) {
        self->tx_count++;
    }

    return 0;
}

static void printStatsAndExit(int signal_number)
{
    (void)signal_number;

    fprintf(
        stderr,
        "[plain] tx=%llu rx=%llu\n",
        (unsigned long long)tunnel.tx_count,
        (unsigned long long)tunnel.rx_count
    );
    _exit(0);
}
