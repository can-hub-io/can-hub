#include <cest>

#include <sys/socket.h>
#include <unistd.h>

extern "C" {
#include "platform/linux/shared/epoll_registry.h"
}

#define PEER_SLOT 3
#define OTHER_SLOT 7
#define FIRST_PEER 100
#define SECOND_PEER 101
#define EVENTS_MAX 4
#define NO_WAIT 0

static EpollRegistry registry;
static int32_t served_fd;
static int32_t remote_fd;

static void connectPeer(void);
static void disconnectPeer(void);
static int32_t readableEvents(void);

describe("epoll_registry", []() {
    beforeEach([]() {
        EpollRegistry_Open(&registry);
        connectPeer();
    });

    afterEach([]() {
        disconnectPeer();
        close(registry.epoll_fd);
    });

    it("reports a registered socket that became readable", []() {
        int32_t event_count;

        EpollRegistry_SyncOccupiedSlot(&registry, PEER_SLOT, FIRST_PEER, served_fd, EPOLLIN, PEER_SLOT);

        event_count = readableEvents();

        expect(event_count).toBe(1);
    });

    it("registers the next peer of a slot although it reuses the fd of the one that left", []() {
        int32_t first_fd = served_fd;
        int32_t event_count;

        EpollRegistry_SyncOccupiedSlot(&registry, PEER_SLOT, FIRST_PEER, served_fd, EPOLLIN, PEER_SLOT);
        disconnectPeer();
        connectPeer();

        EpollRegistry_SyncOccupiedSlot(&registry, PEER_SLOT, SECOND_PEER, served_fd, EPOLLIN, PEER_SLOT);
        event_count = readableEvents();

        expect(served_fd).toBe(first_fd);
        expect(event_count).toBe(1);
    });

    it("keeps a peer registered when the slot that held its fd before is vacated afterwards", []() {
        int32_t event_count;

        EpollRegistry_SyncOccupiedSlot(&registry, PEER_SLOT, FIRST_PEER, served_fd, EPOLLIN, PEER_SLOT);
        disconnectPeer();
        connectPeer();

        EpollRegistry_SyncOccupiedSlot(&registry, OTHER_SLOT, SECOND_PEER, served_fd, EPOLLIN, OTHER_SLOT);
        EpollRegistry_SyncOccupiedSlot(&registry, PEER_SLOT, FIRST_PEER, EPOLL_REGISTRY_NO_SOCKET, EPOLLIN, PEER_SLOT);
        event_count = readableEvents();

        expect(event_count).toBe(1);
    });
});

static void connectPeer(void)
{
    int32_t pair[2];

    socketpair(AF_UNIX, SOCK_STREAM, 0, pair);
    served_fd = pair[0];
    remote_fd = pair[1];
}

static void disconnectPeer(void)
{
    close(served_fd);
    close(remote_fd);
}

static int32_t readableEvents(void)
{
    struct epoll_event events[EVENTS_MAX];
    uint8_t byte = 0;
    ssize_t written;

    written = write(remote_fd, &byte, sizeof(byte));
    if (written != (ssize_t)sizeof(byte)) {
        return -1;
    }

    return EpollRegistry_Wait(&registry, events, EVENTS_MAX, NO_WAIT);
}
