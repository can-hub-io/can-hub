#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sys/epoll.h>

#include "apps/agent/agent_app.h"
#include "platform/linux/clock/clock.h"
#include "platform/linux/quic/quic_client_transport.h"
#include "platform/linux/socketcan/socketcan_adapter.h"
#include "version.h"

#define MAX_EPOLL_EVENTS 16
#define TICK_PERIOD_MS 100
#define QUIC_URL_PREFIX "quic://"

static AgentApp app;
static QuicClientTransport transport;
static SocketCanAdapter can_adapter;
static RegisterMessage registration;

static bool parseArguments(int argc, char **argv, char *host, char *port_text);
static bool parseConnectUrl(const char *url, char *host, char *port_text);
static bool registerPollFds(int32_t epoll_fd);
static void dispatchReadableFd(int32_t event_fd, const CanEvents *can_events);
static void drainCanInterface(uint8_t interface_index, const CanEvents *can_events);

int main(int argc, char **argv)
{
    struct epoll_event events[MAX_EPOLL_EVENTS];
    char host[QUIC_HOST_MAX];
    char port_text[QUIC_PORT_TEXT_MAX];
    TransportEvents transport_events;
    CanEvents can_events;
    int32_t epoll_fd;
    int32_t event_count;
    int32_t i;

    if (!parseArguments(argc, argv, host, port_text)) {
        fprintf(stderr, "usage: %s --connect quic://<host>:<port> [--name <agent-name>] <can-if> [...]\n", argv[0]);
        return 1;
    }

    if (!SocketCanAdapter_Open(&can_adapter, &registration)) {
        fprintf(stderr, "could not open CAN interfaces\n");
        return 1;
    }

    transport_events = AgentApp_TransportEvents(&app);
    can_events = AgentApp_CanEvents(&app);
    if (!QuicClientTransport_Init(&transport, host, port_text, &transport_events)) {
        fprintf(stderr, "could not initialize QUIC transport\n");
        return 1;
    }

    AgentApp_Init(&app, QuicClientTransport_Port(&transport), SocketCanAdapter_Port(&can_adapter), &registration);

    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0 || !registerPollFds(epoll_fd)) {
        return 1;
    }

    fprintf(stderr, "can-hub-agent %s connecting to %s:%s\n", Version_String(), host, port_text);

    for (;;) {
        event_count = epoll_wait(epoll_fd, events, MAX_EPOLL_EVENTS, TICK_PERIOD_MS);

        for(i=0; i<event_count; i++) {
            dispatchReadableFd(events[i].data.fd, &can_events);
        }

        AgentApp_Tick(&app, Clock_RealtimeUs());
    }
}

/* ---------- private ---------- */

static bool parseArguments(int argc, char **argv, char *host, char *port_text)
{
    const char *connect_url = NULL;
    const char *agent_name = "";
    int32_t i;

    memset(&registration, 0, sizeof(registration));

    for(i=1; i<argc; i++) {
        if (strcmp(argv[i], "--connect") == 0 && i + 1 < argc) {
            connect_url = argv[++i];
        } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            agent_name = argv[++i];
        } else {
            if (registration.interface_count >= REGISTER_INTERFACES_MAX) {
                return false;
            }
            snprintf(
                registration.interface_names[registration.interface_count],
                REGISTER_INTERFACE_NAME_SIZE,
                "%s",
                argv[i]
            );
            registration.interface_count++;
        }
    }

    if (connect_url == NULL || registration.interface_count == 0) {
        return false;
    }

    snprintf(registration.agent_name, REGISTER_AGENT_NAME_SIZE, "%s", agent_name);

    return parseConnectUrl(connect_url, host, port_text);
}

static bool parseConnectUrl(const char *url, char *host, char *port_text)
{
    const char *address;
    const char *separator;
    size_t host_length;

    if (strncmp(url, QUIC_URL_PREFIX, strlen(QUIC_URL_PREFIX)) != 0) {
        return false;
    }

    address = url + strlen(QUIC_URL_PREFIX);
    separator = strrchr(address, ':');
    if (separator == NULL || separator == address || separator[1] == '\0') {
        return false;
    }

    host_length = (size_t)(separator - address);
    if (host_length >= QUIC_HOST_MAX) {
        return false;
    }

    memcpy(host, address, host_length);
    host[host_length] = '\0';
    snprintf(port_text, QUIC_PORT_TEXT_MAX, "%s", separator + 1);

    return true;
}

static bool registerPollFds(int32_t epoll_fd)
{
    struct epoll_event event;
    uint8_t interface_index;

    event.events = EPOLLIN;
    event.data.fd = QuicClientTransport_UdpFd(&transport);
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event.data.fd, &event) < 0) {
        return false;
    }
    event.data.fd = QuicClientTransport_TimerFd(&transport);
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event.data.fd, &event) < 0) {
        return false;
    }
    for(interface_index=0; interface_index<registration.interface_count; interface_index++) {
        event.data.fd = SocketCanAdapter_Fd(&can_adapter, interface_index);
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event.data.fd, &event) < 0) {
            return false;
        }
    }

    return true;
}

static void dispatchReadableFd(int32_t event_fd, const CanEvents *can_events)
{
    uint8_t interface_index;

    if (event_fd == QuicClientTransport_UdpFd(&transport)) {
        QuicClientTransport_OnUdpReadable(&transport);
        return;
    }
    if (event_fd == QuicClientTransport_TimerFd(&transport)) {
        QuicClientTransport_OnTimer(&transport);
        return;
    }

    for(interface_index=0; interface_index<registration.interface_count; interface_index++) {
        if (event_fd == SocketCanAdapter_Fd(&can_adapter, interface_index)) {
            drainCanInterface(interface_index, can_events);
            return;
        }
    }
}

static void drainCanInterface(uint8_t interface_index, const CanEvents *can_events)
{
    FrameMessage frame;

    while (SocketCanAdapter_ReadFrame(&can_adapter, interface_index, &frame)) {
        can_events->on_frame(can_events->context, interface_index, &frame);
    }
}
