#include "platform/linux/shared/listen_endpoint.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <arpa/inet.h>
#include <unistd.h>

static bool buildSockaddrIn6(const char *bind_address, const char *port, struct sockaddr_storage *out, socklen_t *out_length);
static bool buildSockaddrIn(const char *bind_address, const char *port, struct sockaddr_storage *out, socklen_t *out_length);

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
    const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)address;
    const struct sockaddr_in *in = (const struct sockaddr_in *)address;
    struct in_addr mapped;
    char ip[INET6_ADDRSTRLEN];

    if (address->ss_family == AF_INET6 && IN6_IS_ADDR_V4MAPPED(&in6->sin6_addr)) {
        memcpy(&mapped, &in6->sin6_addr.s6_addr[12], sizeof(mapped));
        if (inet_ntop(AF_INET, &mapped, ip, sizeof(ip)) == NULL) {
            out[0] = '\0';
            return;
        }
        snprintf(out, size, "%s:%u", ip, ntohs(in6->sin6_port));
        return;
    }

    if (address->ss_family == AF_INET6) {
        if (inet_ntop(AF_INET6, &in6->sin6_addr, ip, sizeof(ip)) == NULL) {
            out[0] = '\0';
            return;
        }
        snprintf(out, size, "[%s]:%u", ip, ntohs(in6->sin6_port));
        return;
    }

    if (inet_ntop(AF_INET, &in->sin_addr, ip, sizeof(ip)) == NULL) {
        out[0] = '\0';
        return;
    }
    snprintf(out, size, "%s:%u", ip, ntohs(in->sin_port));
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
