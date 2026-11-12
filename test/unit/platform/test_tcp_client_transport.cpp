#include <cest>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>

extern "C" {
#include "platform/linux/shared/epoll_registry.h"
#include "platform/linux/tcp/tcp_client_transport.h"
}

#define HUB_SOCKET_PATH "/tmp/can_hub_test_tcp_client_transport.sock"
#define HUB_SLOT 0
#define LISTEN_BACKLOG 4
#define EVENTS_MAX 4
#define NO_WAIT 0

static EpollRegistry registry;
static TcpClientTransport transport;
static int32_t hub_listen_fd;
static int32_t hub_session_fd;

static void openHub(void);
static void closeHub(void);
static void initTransport(void);
static void connectToHub(void);
static void hubDropsSession(void);
static void syncTransportSlot(void);
static int32_t transportEvents(void);
static void reconnectAtOnce(void *context, uint64_t now_us);
static void ignoreConnected(void *context);
static void ignoreControl(void *context, const uint8_t *data, size_t size, uint64_t now_us);
static void ignoreFrame(void *context, const uint8_t *data, size_t size);

describe("tcp_client_transport", []() {
    beforeEach([]() {
        openHub();
        EpollRegistry_Open(&registry);
        initTransport();
    });

    afterEach([]() {
        TcpClientTransport_Port(&transport)->disconnect(&transport);
        close(registry.epoll_fd);
        closeHub();
    });

    it("stays polled when it reconnects on the fd number of the connection it just closed", []() {
        int32_t first_fd;
        int32_t event_count;

        connectToHub();
        first_fd = TcpClientTransport_Fd(&transport);

        hubDropsSession();
        syncTransportSlot();
        hub_session_fd = accept(hub_listen_fd, NULL, NULL);
        event_count = transportEvents();

        expect(TcpClientTransport_Fd(&transport)).toBe(first_fd);
        expect(event_count).toBe(1);
    });
});

static void openHub(void)
{
    struct sockaddr_un address;

    unlink(HUB_SOCKET_PATH);
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, HUB_SOCKET_PATH, sizeof(address.sun_path) - 1);
    hub_listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    bind(hub_listen_fd, (struct sockaddr *)&address, sizeof(address));
    listen(hub_listen_fd, LISTEN_BACKLOG);
    hub_session_fd = -1;
}

static void closeHub(void)
{
    if (hub_session_fd >= 0) {
        close(hub_session_fd);
    }
    close(hub_listen_fd);
    unlink(HUB_SOCKET_PATH);
}

static void initTransport(void)
{
    TransportEvents events;

    events.context = NULL;
    events.on_connected = ignoreConnected;
    events.on_disconnected = reconnectAtOnce;
    events.on_control = ignoreControl;
    events.on_frame = ignoreFrame;
    TcpClientTransport_InitUnix(&transport, HUB_SOCKET_PATH, &events);
}

static void connectToHub(void)
{
    TcpClientTransport_Port(&transport)->connect(&transport);
    syncTransportSlot();
    hub_session_fd = accept(hub_listen_fd, NULL, NULL);
    TcpClientTransport_OnWritable(&transport);
    syncTransportSlot();
}

static void hubDropsSession(void)
{
    close(hub_session_fd);
    hub_session_fd = -1;
    TcpClientTransport_OnReadable(&transport);
}

static void syncTransportSlot(void)
{
    uint32_t wanted_mask = EPOLLIN;

    if (TcpClientTransport_WantsWritable(&transport)) {
        wanted_mask |= EPOLLOUT;
    }
    EpollRegistry_SyncOccupiedSlot(
        &registry,
        HUB_SLOT,
        TcpClientTransport_FdGeneration(&transport),
        TcpClientTransport_Fd(&transport),
        wanted_mask,
        HUB_SLOT
    );
}

static int32_t transportEvents(void)
{
    struct epoll_event events[EVENTS_MAX];

    return EpollRegistry_Wait(&registry, events, EVENTS_MAX, NO_WAIT);
}

static void reconnectAtOnce(void *context, uint64_t now_us)
{
    (void)context;
    (void)now_us;
    TcpClientTransport_Port(&transport)->connect(&transport);
}

static void ignoreConnected(void *context)
{
    (void)context;
}

static void ignoreControl(void *context, const uint8_t *data, size_t size, uint64_t now_us)
{
    (void)context;
    (void)data;
    (void)size;
    (void)now_us;
}

static void ignoreFrame(void *context, const uint8_t *data, size_t size)
{
    (void)context;
    (void)data;
    (void)size;
}
