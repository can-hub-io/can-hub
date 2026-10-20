#include "platform/linux/shared/listen_endpoint.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <arpa/inet.h>
#include <unistd.h>

#define V4_MAPPED_OFFSET 12
#define IPV4_NETWORK_SHIFT 24
#define IPV4_LOOPBACK_NETWORK 127

static bool buildSockaddrIn6(const char *bind_address, const char *port, struct sockaddr_storage *out, socklen_t *out_length);
static bool buildSockaddrIn(const char *bind_address, const char *port, struct sockaddr_storage *out, socklen_t *out_length);
static bool isNativeIpv6(const struct sockaddr_storage *address);
static uint16_t portOf(const struct sockaddr_storage *address);
static bool isLoopbackIpv4(const struct in_addr *address);
static void formatAddress(int32_t family, const void *address, char *out, size_t size);

int32_t ListenEndpoint_OpenSocket(int32_t socktype, int32_t *address_family)
{
    int32_t dual_stack = 0;
    int32_t fd;

    *address_family = AF_INET6;
    fd = socket(AF_INET6, socktype | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        *address_family = AF_INET;
        fd = socket(AF_INET, socktype | SOCK_NONBLOCK, 0);
    }
    if (fd < 0) {
        return fd;
    }
    if (*address_family == AF_INET6) {
        setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &dual_stack, sizeof(dual_stack));
    }

    return fd;
}

bool ListenEndpoint_BuildSockaddr(int32_t address_family, const char *bind_address, const char *port, struct sockaddr_storage *out, socklen_t *out_length)
{
    if (address_family == AF_INET6) {
        return buildSockaddrIn6(bind_address, port, out, out_length);
    }

    return buildSockaddrIn(bind_address, port, out, out_length);
}

void ListenEndpoint_FormatOrigin(const struct sockaddr_storage *address, char *out, size_t size)
{
    char host[INET6_ADDRSTRLEN];

    ListenEndpoint_FormatHost(address, host, sizeof(host));
    if (host[0] == '\0') {
        out[0] = '\0';
        return;
    }
    if (isNativeIpv6(address)) {
        snprintf(out, size, "[%s]:%u", host, portOf(address));
        return;
    }

    snprintf(out, size, "%s:%u", host, portOf(address));
}

void ListenEndpoint_FormatHost(const struct sockaddr_storage *address, char *out, size_t size)
{
    const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)address;
    const struct sockaddr_in *in = (const struct sockaddr_in *)address;
    struct in_addr mapped;

    if (isNativeIpv6(address)) {
        formatAddress(AF_INET6, &in6->sin6_addr, out, size);
        return;
    }
    if (address->ss_family == AF_INET6) {
        memcpy(&mapped, &in6->sin6_addr.s6_addr[V4_MAPPED_OFFSET], sizeof(mapped));
        formatAddress(AF_INET, &mapped, out, size);
        return;
    }

    formatAddress(AF_INET, &in->sin_addr, out, size);
}

bool ListenEndpoint_IsLoopback(const char *bind_address)
{
    struct in6_addr v6;
    struct in_addr v4;

    if (inet_pton(AF_INET, bind_address, &v4) == 1) {
        return isLoopbackIpv4(&v4);
    }
    if (inet_pton(AF_INET6, bind_address, &v6) != 1) {
        return false;
    }
    if (IN6_IS_ADDR_V4MAPPED(&v6)) {
        memcpy(&v4, &v6.s6_addr[V4_MAPPED_OFFSET], sizeof(v4));
        return isLoopbackIpv4(&v4);
    }

    return IN6_IS_ADDR_LOOPBACK(&v6);
}

static bool buildSockaddrIn6(const char *bind_address, const char *port, struct sockaddr_storage *out, socklen_t *out_length)
{
    struct sockaddr_in6 *address = (struct sockaddr_in6 *)out;
    struct in_addr v4;

    memset(out, 0, sizeof(*out));
    address->sin6_family = AF_INET6;
    address->sin6_port = htons((uint16_t)atoi(port));
    *out_length = sizeof(*address);

    if (strcmp(bind_address, "0.0.0.0") == 0 || strcmp(bind_address, "::") == 0) {
        address->sin6_addr = in6addr_any;
        return true;
    }
    if (inet_pton(AF_INET6, bind_address, &address->sin6_addr) == 1) {
        return true;
    }
    if (inet_pton(AF_INET, bind_address, &v4) == 1) {
        address->sin6_addr.s6_addr[10] = 0xFF;
        address->sin6_addr.s6_addr[11] = 0xFF;
        memcpy(&address->sin6_addr.s6_addr[12], &v4, sizeof(v4));
        return true;
    }

    return false;
}

static bool buildSockaddrIn(const char *bind_address, const char *port, struct sockaddr_storage *out, socklen_t *out_length)
{
    struct sockaddr_in *address = (struct sockaddr_in *)out;

    memset(out, 0, sizeof(*out));
    address->sin_family = AF_INET;
    address->sin_port = htons((uint16_t)atoi(port));
    *out_length = sizeof(*address);

    if (strcmp(bind_address, "0.0.0.0") == 0) {
        address->sin_addr.s_addr = htonl(INADDR_ANY);
        return true;
    }

    return inet_pton(AF_INET, bind_address, &address->sin_addr) == 1;
}

static bool isNativeIpv6(const struct sockaddr_storage *address)
{
    const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)address;

    return address->ss_family == AF_INET6 && !IN6_IS_ADDR_V4MAPPED(&in6->sin6_addr);
}

static uint16_t portOf(const struct sockaddr_storage *address)
{
    const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)address;
    const struct sockaddr_in *in = (const struct sockaddr_in *)address;

    if (address->ss_family == AF_INET6) {
        return ntohs(in6->sin6_port);
    }

    return ntohs(in->sin_port);
}

static bool isLoopbackIpv4(const struct in_addr *address)
{
    return (ntohl(address->s_addr) >> IPV4_NETWORK_SHIFT) == IPV4_LOOPBACK_NETWORK;
}

static void formatAddress(int32_t family, const void *address, char *out, size_t size)
{
    if (inet_ntop(family, address, out, (socklen_t)size) == NULL) {
        out[0] = '\0';
    }
}
