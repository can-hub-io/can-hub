#include "platform/linux/shared/host_resolver.h"

#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/eventfd.h>

#include "platform/linux/shared/log.h"

#define RESOLUTION_HOST_MAX 256
#define RESOLUTION_PORT_TEXT_MAX 16
#define RESOLUTION_SIGNAL 1

typedef enum tresolution_stage_e {
    kRESOLUTION_STAGE_RESOLVING,
    kRESOLUTION_STAGE_SIGNALLING,
    kRESOLUTION_STAGE_DONE,
    kRESOLUTION_STAGE_ABANDONED,
    kRESOLUTION_STAGE_MAX
} TRESOLUTION_STAGE;

struct HostResolution {
    char host[RESOLUTION_HOST_MAX];
    char port_text[RESOLUTION_PORT_TEXT_MAX];
    struct addrinfo hints;
    struct addrinfo *addresses;
    int32_t status;
    int32_t event_fd;
    _Atomic uint8_t stage;
};

static HostResolution *newResolution(const char *host, const char *port, const struct addrinfo *hints);
static bool spawnResolverThread(HostResolution *resolution);
static void *resolveOnThread(void *argument);
static bool publishResult(HostResolution *resolution);
static void signalOwner(HostResolution *resolution);
static void freeResolution(HostResolution *resolution);

/* ---------- public ---------- */

bool HostResolver_Start(HostResolver *self, const char *host, const char *port, const struct addrinfo *hints)
{
    HostResolution *resolution;

    HostResolver_Release(self);
    self->unresolved = false;

    resolution = newResolution(host, port, hints);
    if (resolution == NULL) {
        return false;
    }
    if (!spawnResolverThread(resolution)) {
        freeResolution(resolution);
        return false;
    }

    self->pending = resolution;

    return true;
}

bool HostResolver_IsPending(const HostResolver *self)
{
    return self->pending != NULL;
}

int32_t HostResolver_Fd(const HostResolver *self)
{
    if (self->pending == NULL) {
        return HOST_RESOLVER_NO_FD;
    }

    return self->pending->event_fd;
}

bool HostResolver_IsFinished(const HostResolver *self)
{
    uint8_t stage;

    if (self->pending == NULL) {
        return false;
    }

    stage = atomic_load(&self->pending->stage);

    return stage == kRESOLUTION_STAGE_SIGNALLING || stage == kRESOLUTION_STAGE_DONE;
}

const struct addrinfo *HostResolver_Addresses(HostResolver *self)
{
    HostResolution *resolution = self->pending;

    if (!HostResolver_IsFinished(self)) {
        return NULL;
    }
    if (resolution->status == 0) {
        return resolution->addresses;
    }

    self->unresolved = true;
    LOG_WARN("could not resolve %s: %s", resolution->host, gai_strerror(resolution->status));

    return NULL;
}

void HostResolver_Release(HostResolver *self)
{
    HostResolution *resolution = self->pending;

    if (resolution == NULL) {
        return;
    }

    self->pending = NULL;
    if (atomic_exchange(&resolution->stage, kRESOLUTION_STAGE_ABANDONED) == kRESOLUTION_STAGE_DONE) {
        freeResolution(resolution);
    }
}

bool HostResolver_HostUnresolved(const HostResolver *self)
{
    return self->unresolved;
}

/* ---------- private ---------- */

static HostResolution *newResolution(const char *host, const char *port, const struct addrinfo *hints)
{
    HostResolution *resolution = calloc(1, sizeof(*resolution));

    if (resolution == NULL) {
        return NULL;
    }

    resolution->event_fd = eventfd(0, EFD_NONBLOCK);
    if (resolution->event_fd < 0) {
        free(resolution);
        return NULL;
    }

    snprintf(resolution->host, sizeof(resolution->host), "%s", host);
    snprintf(resolution->port_text, sizeof(resolution->port_text), "%s", port);
    resolution->hints = *hints;
    atomic_init(&resolution->stage, kRESOLUTION_STAGE_RESOLVING);

    return resolution;
}

static bool spawnResolverThread(HostResolution *resolution)
{
    pthread_attr_t attributes;
    pthread_t thread;
    sigset_t all_signals;
    sigset_t caller_signals;
    bool spawned;

    if (pthread_attr_init(&attributes) != 0) {
        return false;
    }

    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    sigfillset(&all_signals);
    pthread_sigmask(SIG_SETMASK, &all_signals, &caller_signals);
    spawned = pthread_create(&thread, &attributes, resolveOnThread, resolution) == 0;
    pthread_sigmask(SIG_SETMASK, &caller_signals, NULL);
    pthread_attr_destroy(&attributes);

    return spawned;
}

static void *resolveOnThread(void *argument)
{
    HostResolution *resolution = argument;

    resolution->status = getaddrinfo(resolution->host, resolution->port_text, &resolution->hints, &resolution->addresses);
    if (!publishResult(resolution)) {
        freeResolution(resolution);
        return NULL;
    }

    signalOwner(resolution);
    if (atomic_exchange(&resolution->stage, kRESOLUTION_STAGE_DONE) == kRESOLUTION_STAGE_ABANDONED) {
        freeResolution(resolution);
    }

    return NULL;
}

static bool publishResult(HostResolution *resolution)
{
    uint8_t expected = kRESOLUTION_STAGE_RESOLVING;

    return atomic_compare_exchange_strong(&resolution->stage, &expected, kRESOLUTION_STAGE_SIGNALLING);
}

static void signalOwner(HostResolution *resolution)
{
    uint64_t signal_value = RESOLUTION_SIGNAL;
    ssize_t bytes_written;

    bytes_written = write(resolution->event_fd, &signal_value, sizeof(signal_value));
    (void)bytes_written;
}

static void freeResolution(HostResolution *resolution)
{
    if (resolution->addresses != NULL) {
        freeaddrinfo(resolution->addresses);
    }

    close(resolution->event_fd);
    free(resolution);
}
