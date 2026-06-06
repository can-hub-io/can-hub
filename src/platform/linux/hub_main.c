#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sys/epoll.h>

#include "apps/hub/hub_app.h"
#include "platform/linux/tcp/tcp_server_transport.h"
#include "version.h"

#define MAX_EPOLL_EVENTS 32
#define POLL_PERIOD_MS 100
#define TCP_URL_PREFIX "tcp://"
#define LISTEN_PORT_MAX 16
#define NO_SOCKET (-1)
#define LISTENER_TAG UINT32_MAX

static HubApp app;
static TcpServerTransport transport;
static int32_t registered_fds[TCP_SERVER_PEERS_MAX];
static uint32_t registered_masks[TCP_SERVER_PEERS_MAX];

static bool parseArguments(int argc, char **argv, char *listen_port);
static void syncSlotRegistrations(int32_t epoll_fd);
static void dispatchEvent(const struct epoll_event *event);

int main(int argc, char **argv)
{
    struct epoll_event events[MAX_EPOLL_EVENTS];
    struct epoll_event event;
    char listen_port[LISTEN_PORT_MAX];
    HubTransportEvents transport_events;
    int32_t epoll_fd;
    int32_t event_count;
    int32_t i;
    uint8_t slot;

    if (!parseArguments(argc, argv, listen_port)) {
        fprintf(stderr, "usage: %s --listen tcp://<port>\n", argv[0]);
        return 1;
    }

    transport_events = HubApp_Events(&app);
    if (!TcpServerTransport_Init(&transport, listen_port, &transport_events)) {
        fprintf(stderr, "could not open TCP listener on port %s\n", listen_port);
        return 1;
    }
    HubApp_Init(&app, TcpServerTransport_Port(&transport));

    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        return 1;
    }
    event.events = EPOLLIN;
    event.data.u32 = LISTENER_TAG;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, TcpServerTransport_ListenFd(&transport), &event) < 0) {
        return 1;
    }
    for(slot=0; slot<TCP_SERVER_PEERS_MAX; slot++) {
        registered_fds[slot] = NO_SOCKET;
    }

    fprintf(stderr, "can-hub %s listening on tcp port %s\n", Version_String(), listen_port);

    for (;;) {
        syncSlotRegistrations(epoll_fd);
        event_count = epoll_wait(epoll_fd, events, MAX_EPOLL_EVENTS, POLL_PERIOD_MS);

        for(i=0; i<event_count; i++) {
            dispatchEvent(&events[i]);
        }
    }
}

/* ---------- private ---------- */

static bool parseArguments(int argc, char **argv, char *listen_port)
{
    const char *listen_url = NULL;
    int32_t i;

    for(i=1; i<argc; i++) {
        if (strcmp(argv[i], "--listen") == 0 && i + 1 < argc) {
            listen_url = argv[++i];
        }
    }

    if (listen_url == NULL || strncmp(listen_url, TCP_URL_PREFIX, strlen(TCP_URL_PREFIX)) != 0) {
        return false;
    }

    snprintf(listen_port, LISTEN_PORT_MAX, "%s", listen_url + strlen(TCP_URL_PREFIX));

    return listen_port[0] != '\0';
}

static void syncSlotRegistrations(int32_t epoll_fd)
{
    struct epoll_event event;
    int32_t current_fd;
    uint32_t wanted_mask;
    uint8_t slot;
    bool unchanged;

    for(slot=0; slot<TCP_SERVER_PEERS_MAX; slot++) {
        current_fd = TcpServerTransport_SlotFd(&transport, slot);
        wanted_mask = EPOLLIN;
        if (TcpServerTransport_SlotWantsWritable(&transport, slot)) {
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

    if (event->data.u32 == LISTENER_TAG) {
        TcpServerTransport_OnAcceptReady(&transport);
        return;
    }

    slot = (uint8_t)event->data.u32;
    if (event->events & EPOLLOUT) {
        TcpServerTransport_OnSlotWritable(&transport, slot);
    }
    if (event->events & EPOLLIN) {
        TcpServerTransport_OnSlotReadable(&transport, slot);
    }
}
