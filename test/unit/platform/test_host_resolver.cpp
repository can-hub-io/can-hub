#include <cest>

#include <cstring>

extern "C" {
#include "platform/linux/shared/host_resolver.h"
}

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>

#define HUB_ADDRESS "127.0.0.1"
#define HUB_PORT 7227
#define HUB_PORT_TEXT "7227"
#define UNRESOLVABLE_HOST "hub.invalid"
#define RESOLUTION_TIMEOUT_MS 5000

static HostResolver resolver;
static bool started;
static bool signalled;
static bool finished;
static bool unresolved;
static bool pending;
static int32_t resolver_fd;
static int32_t resolved_family;
static uint16_t resolved_port;

static void startResolving(const char *host);
static void awaitResolution(void);
static void collectAddresses(void);
static void releaseResolver(void);

describe("HostResolver", []() {
    beforeEach([]() {
        memset(&resolver, 0, sizeof(resolver));
        resolved_family = AF_UNSPEC;
        resolved_port = 0;
    });

    it("signals its fd once the addresses of the host are in", []() {
        startResolving(HUB_ADDRESS);

        awaitResolution();
        collectAddresses();
        releaseResolver();

        expect(started).toBe(true);
        expect(signalled).toBe(true);
        expect(finished).toBe(true);
        expect(resolved_family).toBe((int32_t)AF_INET);
        expect(resolved_port).toBe((uint16_t)HUB_PORT);
        expect(unresolved).toBe(false);
    });

    it("reports a host that does not resolve", []() {
        startResolving(UNRESOLVABLE_HOST);

        awaitResolution();
        collectAddresses();
        releaseResolver();

        expect(finished).toBe(true);
        expect(resolved_family).toBe((int32_t)AF_UNSPEC);
        expect(unresolved).toBe(true);
    });

    it("goes idle when a resolution still in flight is released", []() {
        startResolving(HUB_ADDRESS);

        releaseResolver();

        expect(pending).toBe(false);
        expect(resolver_fd).toBe((int32_t)HOST_RESOLVER_NO_FD);
    });
});

static void startResolving(const char *host)
{
    struct addrinfo hints;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICHOST;
    started = HostResolver_Start(&resolver, host, HUB_PORT_TEXT, &hints);
}

static void awaitResolution(void)
{
    struct pollfd watched;

    watched.fd = HostResolver_Fd(&resolver);
    watched.events = POLLIN;
    watched.revents = 0;
    signalled = poll(&watched, 1, RESOLUTION_TIMEOUT_MS) == 1;
    finished = HostResolver_IsFinished(&resolver);
}

static void collectAddresses(void)
{
    const struct addrinfo *addresses = HostResolver_Addresses(&resolver);
    const struct sockaddr_in *address;

    unresolved = HostResolver_HostUnresolved(&resolver);
    if (addresses == NULL) {
        return;
    }

    address = (const struct sockaddr_in *)addresses->ai_addr;
    resolved_family = addresses->ai_family;
    resolved_port = ntohs(address->sin_port);
}

static void releaseResolver(void)
{
    HostResolver_Release(&resolver);
    pending = HostResolver_IsPending(&resolver);
    resolver_fd = HostResolver_Fd(&resolver);
}
