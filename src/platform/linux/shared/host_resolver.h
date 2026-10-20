#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <netdb.h>

#define HOST_RESOLVER_NO_FD (-1)

typedef struct HostResolution HostResolution;

/*
 * Resolves one host name off the event loop: getaddrinfo runs on a detached
 * thread that signals an eventfd when it returns. While a resolution is
 * pending the transport hands Fd() to the platform loop in place of its
 * connection fd, and collects the addresses once it is readable. Releasing a
 * resolution still in flight hands it over to its thread, which frees it on
 * return, so the owner never waits on a slow resolver.
 */
typedef struct {
    HostResolution *pending;
    bool unresolved;
} HostResolver;

bool HostResolver_Start(HostResolver *self, const char *host, const char *port, const struct addrinfo *hints);
bool HostResolver_IsPending(const HostResolver *self);
int32_t HostResolver_Fd(const HostResolver *self);
bool HostResolver_IsFinished(const HostResolver *self);
const struct addrinfo *HostResolver_Addresses(HostResolver *self);
void HostResolver_Release(HostResolver *self);
bool HostResolver_HostUnresolved(const HostResolver *self);
