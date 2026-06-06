#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sys/epoll.h>

#include "hub/hub_app.h"
#include "platform/linux/quic/quic_server_transport.h"
#include "platform/linux/tcp/tcp_server_transport.h"
#include "version.h"

#define MAX_EPOLL_EVENTS 32
#define POLL_PERIOD_MS 100
#define TCP_URL_PREFIX "tcp://"
#define QUIC_URL_PREFIX "quic://"
#define LISTEN_PORT_MAX 16
#define NO_SOCKET (-1)
#define TCP_PEER_ID_BASE 0x00000001
#define QUIC_PEER_ID_BASE 0x80000001

typedef enum thub_event_tag_e {
    kHUB_EVENT_TAG_TCP_LISTENER = 0x100,
    kHUB_EVENT_TAG_QUIC_UDP,
    kHUB_EVENT_TAG_QUIC_TIMER,
    kHUB_EVENT_TAG_MAX,
} THUB_EVENT_TAG;

static HubApp app;
static TcpServerTransport tcp_transport;
static QuicServerTransport quic_transport;
static HubTransportPort mux_port;
static bool tcp_enabled;
static bool quic_enabled;
static int32_t registered_fds[TCP_SERVER_PEERS_MAX];
static uint32_t registered_masks[TCP_SERVER_PEERS_MAX];

static bool parseArguments(int argc, char **argv);
static bool startListeners(const char *tcp_port, const char *quic_port, const char *certificate, const char *key);
static bool muxSendControl(void *context, uint32_t peer_id, const uint8_t *data, size_t size);
static bool muxSendFrame(void *context, uint32_t peer_id, const uint8_t *data, size_t size);
static void muxClosePeer(void *context, uint32_t peer_id);
static HubTransportPort *portForPeer(uint32_t peer_id);
static bool registerStaticPollFds(int32_t epoll_fd);
static void syncTcpSlotRegistrations(int32_t epoll_fd);
static void dispatchEvent(const struct epoll_event *event);

int main(int argc, char **argv)
{
    struct epoll_event events[MAX_EPOLL_EVENTS];
    int32_t epoll_fd;
    int32_t event_count;
    int32_t i;
    uint8_t slot;

    if (!parseArguments(argc, argv)) {
        fprintf(
            stderr,
            "usage: %s [--listen tcp://<port>] [--listen quic://<port> --cert <pem> --key <pem>]\n",
            argv[0]
        );
        return 1;
    }

    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0 || !registerStaticPollFds(epoll_fd)) {
        return 1;
    }
    for(slot=0; slot<TCP_SERVER_PEERS_MAX; slot++) {
        registered_fds[slot] = NO_SOCKET;
    }

    fprintf(
        stderr,
        "can-hub %s ready (tcp:%s quic:%s)\n",
        Version_String(),
        tcp_enabled ? "on" : "off",
        quic_enabled ? "on" : "off"
    );

    for (;;) {
        if (tcp_enabled) {
            syncTcpSlotRegistrations(epoll_fd);
        }
        event_count = epoll_wait(epoll_fd, events, MAX_EPOLL_EVENTS, POLL_PERIOD_MS);

        for(i=0; i<event_count; i++) {
            dispatchEvent(&events[i]);
        }
    }
}

/* ---------- private ---------- */

static bool parseArguments(int argc, char **argv)
{
    const char *tcp_port = NULL;
    const char *quic_port = NULL;
    const char *certificate = NULL;
    const char *key = NULL;
    const char *url;
    int32_t i;

    for(i=1; i<argc; i++) {
        if (strcmp(argv[i], "--listen") == 0 && i + 1 < argc) {
            url = argv[++i];
            if (strncmp(url, TCP_URL_PREFIX, strlen(TCP_URL_PREFIX)) == 0) {
                tcp_port = url + strlen(TCP_URL_PREFIX);
            } else if (strncmp(url, QUIC_URL_PREFIX, strlen(QUIC_URL_PREFIX)) == 0) {
                quic_port = url + strlen(QUIC_URL_PREFIX);
            } else {
                return false;
            }
        } else if (strcmp(argv[i], "--cert") == 0 && i + 1 < argc) {
            certificate = argv[++i];
        } else if (strcmp(argv[i], "--key") == 0 && i + 1 < argc) {
            key = argv[++i];
        }
    }

    if (tcp_port == NULL && quic_port == NULL) {
        return false;
    }
    if (quic_port != NULL && (certificate == NULL || key == NULL)) {
        return false;
    }

    return startListeners(tcp_port, quic_port, certificate, key);
}

static bool startListeners(const char *tcp_port, const char *quic_port, const char *certificate, const char *key)
{
    HubTransportEvents transport_events = HubApp_Events(&app);

    mux_port.context = NULL;
    mux_port.send_control = muxSendControl;
    mux_port.send_frame = muxSendFrame;
    mux_port.close_peer = muxClosePeer;

    if (tcp_port != NULL) {
        if (!TcpServerTransport_Init(&tcp_transport, tcp_port, TCP_PEER_ID_BASE, &transport_events)) {
            fprintf(stderr, "could not open TCP listener on port %s\n", tcp_port);
            return false;
        }
        tcp_enabled = true;
    }
    if (quic_port != NULL) {
        bool quic_started = QuicServerTransport_Init(
            &quic_transport,
            quic_port,
            certificate,
            key,
            QUIC_PEER_ID_BASE,
            &transport_events
        );
        if (!quic_started) {
            fprintf(stderr, "could not open QUIC listener on port %s\n", quic_port);
            return false;
        }
        quic_enabled = true;
    }

    HubApp_Init(&app, &mux_port);

    return true;
}

static bool muxSendControl(void *context, uint32_t peer_id, const uint8_t *data, size_t size)
{
    HubTransportPort *destination = portForPeer(peer_id);

    (void)context;

    return destination->send_control(destination->context, peer_id, data, size);
}

static bool muxSendFrame(void *context, uint32_t peer_id, const uint8_t *data, size_t size)
{
    HubTransportPort *destination = portForPeer(peer_id);

    (void)context;

    return destination->send_frame(destination->context, peer_id, data, size);
}

static void muxClosePeer(void *context, uint32_t peer_id)
{
    HubTransportPort *destination = portForPeer(peer_id);

    (void)context;

    destination->close_peer(destination->context, peer_id);
}

static HubTransportPort *portForPeer(uint32_t peer_id)
{
    if (peer_id >= QUIC_PEER_ID_BASE) {
        return QuicServerTransport_Port(&quic_transport);
    }

    return TcpServerTransport_Port(&tcp_transport);
}

static bool registerStaticPollFds(int32_t epoll_fd)
{
    struct epoll_event event;

    event.events = EPOLLIN;
    if (tcp_enabled) {
        event.data.u32 = kHUB_EVENT_TAG_TCP_LISTENER;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, TcpServerTransport_ListenFd(&tcp_transport), &event) < 0) {
            return false;
        }
    }
    if (quic_enabled) {
        event.data.u32 = kHUB_EVENT_TAG_QUIC_UDP;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, QuicServerTransport_UdpFd(&quic_transport), &event) < 0) {
            return false;
        }
        event.data.u32 = kHUB_EVENT_TAG_QUIC_TIMER;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, QuicServerTransport_TimerFd(&quic_transport), &event) < 0) {
            return false;
        }
    }

    return true;
}

static void syncTcpSlotRegistrations(int32_t epoll_fd)
{
    struct epoll_event event;
    int32_t current_fd;
    uint32_t wanted_mask;
    uint8_t slot;
    bool unchanged;

    for(slot=0; slot<TCP_SERVER_PEERS_MAX; slot++) {
        current_fd = TcpServerTransport_SlotFd(&tcp_transport, slot);
        wanted_mask = EPOLLIN;
        if (TcpServerTransport_SlotWantsWritable(&tcp_transport, slot)) {
            wanted_mask |= EPOLLOUT;
        }

        unchanged = current_fd == registered_fds[slot]
                    && (current_fd == NO_SOCKET || wanted_mask == registered_masks[slot]);
        if (unchanged) {
            continue;
        }

        if (registered_fds[slot] != NO_SOCKET && registered_fds[slot] != current_fd) {
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, registered_fds[slot], NULL);
            registered_fds[slot] = NO_SOCKET;
        }
        if (current_fd == NO_SOCKET) {
            registered_fds[slot] = NO_SOCKET;
            continue;
        }

        event.events = wanted_mask;
        event.data.u32 = slot;
        if (registered_fds[slot] == current_fd) {
            epoll_ctl(epoll_fd, EPOLL_CTL_MOD, current_fd, &event);
        } else {
            epoll_ctl(epoll_fd, EPOLL_CTL_ADD, current_fd, &event);
        }
        registered_fds[slot] = current_fd;
        registered_masks[slot] = wanted_mask;
    }
}

static void dispatchEvent(const struct epoll_event *event)
{
    uint8_t slot;

    if (event->data.u32 == kHUB_EVENT_TAG_TCP_LISTENER) {
        TcpServerTransport_OnAcceptReady(&tcp_transport);
        return;
    }
    if (event->data.u32 == kHUB_EVENT_TAG_QUIC_UDP) {
        QuicServerTransport_OnUdpReadable(&quic_transport);
        return;
    }
    if (event->data.u32 == kHUB_EVENT_TAG_QUIC_TIMER) {
        QuicServerTransport_OnTimer(&quic_transport);
        return;
    }

    slot = (uint8_t)event->data.u32;
    if (event->events & EPOLLOUT) {
        TcpServerTransport_OnSlotWritable(&tcp_transport, slot);
    }
    if (event->events & EPOLLIN) {
        TcpServerTransport_OnSlotReadable(&tcp_transport, slot);
    }
}
