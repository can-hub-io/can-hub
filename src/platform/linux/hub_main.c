#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sys/epoll.h>

#include "hub/hub_app.h"
#include "platform/linux/quic/quic_server_transport.h"
#include "platform/linux/shared/connect_url.h"
#include "platform/linux/shared/epoll_registry.h"
#include "platform/linux/tcp/tcp_server_transport.h"
#include "version.h"

#define MAX_EPOLL_EVENTS 32
#define POLL_PERIOD_MS 100
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
static EpollRegistry poll_registry;

static bool parseArguments(int argc, char **argv);
static bool startListeners(const char *tcp_port, const char *quic_port, const char *certificate, const char *key);
static bool muxSendControl(void *context, uint32_t peer_id, const uint8_t *data, size_t size);
static bool muxSendFrame(void *context, uint32_t peer_id, const uint8_t *data, size_t size);
static void muxClosePeer(void *context, uint32_t peer_id);
static HubTransportPort *portForPeer(uint32_t peer_id);
static bool registerStaticPollFds(void);
static void syncTcpSlotRegistrations(void);
static void dispatchEvent(const struct epoll_event *event);

int main(int argc, char **argv)
{
    struct epoll_event events[MAX_EPOLL_EVENTS];
    int32_t event_count;
    int32_t i;

    if (!parseArguments(argc, argv)) {
        fprintf(
            stderr,
            "usage: %s [--listen tcp://<port>] [--listen quic://<port> --cert <pem> --key <pem>]\n",
            argv[0]
        );
        return 1;
    }

    if (!EpollRegistry_Open(&poll_registry) || !registerStaticPollFds()) {
        return 1;
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
            syncTcpSlotRegistrations();
        }
        event_count = EpollRegistry_Wait(&poll_registry, events, MAX_EPOLL_EVENTS, POLL_PERIOD_MS);

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
    const char *port;
    uint8_t scheme;
    int32_t i;

    for(i=1; i<argc; i++) {
        if (strcmp(argv[i], "--listen") == 0 && i + 1 < argc) {
            if (!ConnectUrl_ParseScheme(argv[++i], &scheme, &port)) {
                return false;
            }
            if (scheme == kCONNECT_SCHEME_TCP) {
                tcp_port = port;
            } else {
                quic_port = port;
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

static bool registerStaticPollFds(void)
{
    if (tcp_enabled) {
        int32_t listen_fd = TcpServerTransport_ListenFd(&tcp_transport);
        if (!EpollRegistry_AddStatic(&poll_registry, listen_fd, kHUB_EVENT_TAG_TCP_LISTENER)) {
            return false;
        }
    }
    if (quic_enabled) {
        int32_t udp_fd = QuicServerTransport_UdpFd(&quic_transport);
        int32_t timer_fd = QuicServerTransport_TimerFd(&quic_transport);
        if (!EpollRegistry_AddStatic(&poll_registry, udp_fd, kHUB_EVENT_TAG_QUIC_UDP)) {
            return false;
        }
        if (!EpollRegistry_AddStatic(&poll_registry, timer_fd, kHUB_EVENT_TAG_QUIC_TIMER)) {
            return false;
        }
    }

    return true;
}

static void syncTcpSlotRegistrations(void)
{
    int32_t current_fd;
    uint32_t wanted_mask;
    uint8_t slot;

    for(slot=0; slot<TCP_SERVER_PEERS_MAX; slot++) {
        current_fd = TcpServerTransport_SlotFd(&tcp_transport, slot);
        wanted_mask = EPOLLIN;
        if (TcpServerTransport_SlotWantsWritable(&tcp_transport, slot)) {
            wanted_mask |= EPOLLOUT;
        }

        EpollRegistry_SyncSlot(&poll_registry, slot, current_fd, wanted_mask, slot);
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
