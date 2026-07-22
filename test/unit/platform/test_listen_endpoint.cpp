#include <cest>

#include <cstring>

extern "C" {
#include "platform/linux/shared/listen_endpoint.h"
}

#include <arpa/inet.h>

static struct sockaddr_storage address;
static socklen_t address_length;
static bool built;
static char origin[64];

describe("listen_endpoint_build_sockaddr", []() {
    it("maps the any address to the IPv6 wildcard for a dual-stack socket", []() {
        const struct sockaddr_in6 *in6;

        built = ListenEndpoint_BuildSockaddr(AF_INET6, "0.0.0.0", "7227", &address, &address_length);
        in6 = (const struct sockaddr_in6 *)&address;

        expect(built).toBe(true);
        expect((int)in6->sin6_family).toBe((int)AF_INET6);
        expect((int)ntohs(in6->sin6_port)).toBe(7227);
        expect((bool)IN6_IS_ADDR_UNSPECIFIED(&in6->sin6_addr)).toBe(true);
    });

    it("keeps a literal IPv6 address on a dual-stack socket", []() {
        const struct sockaddr_in6 *in6;

        built = ListenEndpoint_BuildSockaddr(AF_INET6, "::1", "7227", &address, &address_length);
        in6 = (const struct sockaddr_in6 *)&address;

        expect(built).toBe(true);
        expect((bool)IN6_IS_ADDR_LOOPBACK(&in6->sin6_addr)).toBe(true);
    });

    it("maps a literal IPv4 address into a v4-mapped address on a dual-stack socket", []() {
        const struct sockaddr_in6 *in6;

        built = ListenEndpoint_BuildSockaddr(AF_INET6, "127.0.0.1", "7228", &address, &address_length);
        in6 = (const struct sockaddr_in6 *)&address;

        expect(built).toBe(true);
        expect((bool)IN6_IS_ADDR_V4MAPPED(&in6->sin6_addr)).toBe(true);
    });

    it("binds the any address on an IPv4-only fallback socket", []() {
        const struct sockaddr_in *in;

        built = ListenEndpoint_BuildSockaddr(AF_INET, "0.0.0.0", "7227", &address, &address_length);
        in = (const struct sockaddr_in *)&address;

        expect(built).toBe(true);
        expect((int)in->sin_family).toBe((int)AF_INET);
        expect((uint32_t)in->sin_addr.s_addr).toBe((uint32_t)htonl(INADDR_ANY));
    });

    it("rejects an IPv6 literal on an IPv4-only fallback socket", []() {
        built = ListenEndpoint_BuildSockaddr(AF_INET, "::1", "7227", &address, &address_length);

        expect(built).toBe(false);
    });
});

describe("listen_endpoint_format_origin", []() {
    it("renders a plain IPv4 peer", []() {
        ListenEndpoint_BuildSockaddr(AF_INET, "10.0.0.5", "7228", &address, &address_length);
        ListenEndpoint_FormatOrigin(&address, origin, sizeof(origin));

        expect((const char *)origin).toBe("10.0.0.5:7228");
    });

    it("unmaps a v4-mapped peer back to plain IPv4", []() {
        ListenEndpoint_BuildSockaddr(AF_INET6, "127.0.0.1", "7227", &address, &address_length);
        ListenEndpoint_FormatOrigin(&address, origin, sizeof(origin));

        expect((const char *)origin).toBe("127.0.0.1:7227");
    });

    it("renders a native IPv6 peer in brackets", []() {
        ListenEndpoint_BuildSockaddr(AF_INET6, "::1", "7227", &address, &address_length);
        ListenEndpoint_FormatOrigin(&address, origin, sizeof(origin));

        expect((const char *)origin).toBe("[::1]:7227");
    });
});
