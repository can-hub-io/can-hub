#ifndef PLATFORM_LINUX_SHARED_EPOLL_REGISTRY_H
#define PLATFORM_LINUX_SHARED_EPOLL_REGISTRY_H

#include <stdbool.h>
#include <stdint.h>

#include <sys/epoll.h>

#define EPOLL_REGISTRY_SLOTS_MAX 192
#define EPOLL_REGISTRY_NO_SOCKET (-1)

/*
 * Keeps epoll registrations in sync with fds that appear, change interest
 * mask or vanish between loop iterations (reconnecting clients, peer slots).
 * Every event carries a caller-chosen u32 tag in epoll_data.u32.
 */
typedef struct EpollRegistry {
    int32_t epoll_fd;
    int32_t registered_fds[EPOLL_REGISTRY_SLOTS_MAX];
    uint32_t registered_masks[EPOLL_REGISTRY_SLOTS_MAX];
} EpollRegistry;

bool EpollRegistry_Open(EpollRegistry *self);
bool EpollRegistry_AddStatic(EpollRegistry *self, int32_t fd, uint32_t event_data);
void EpollRegistry_SyncSlot(
    EpollRegistry *self,
    uint8_t slot,
    int32_t current_fd,
    uint32_t wanted_mask,
    uint32_t event_data
);
int32_t EpollRegistry_Wait(
    EpollRegistry *self,
    struct epoll_event *events,
    int32_t max_events,
    int32_t timeout_ms
);

#endif
