#include "platform/linux/shared/epoll_registry.h"

#include <stddef.h>

bool EpollRegistry_Open(EpollRegistry *self)
{
    uint8_t slot;

    self->epoll_fd = epoll_create1(0);
    for(slot=0; slot<EPOLL_REGISTRY_SLOTS_MAX; slot++) {
        self->registered_fds[slot] = EPOLL_REGISTRY_NO_SOCKET;
        self->registered_masks[slot] = 0;
    }

    return self->epoll_fd >= 0;
}

bool EpollRegistry_AddStatic(EpollRegistry *self, int32_t fd, uint32_t event_data)
{
    struct epoll_event event;

    event.events = EPOLLIN;
    event.data.u32 = event_data;

    return epoll_ctl(self->epoll_fd, EPOLL_CTL_ADD, fd, &event) == 0;
}

void EpollRegistry_SetStaticInterest(EpollRegistry *self, int32_t fd, uint32_t mask, uint32_t event_data)
{
    struct epoll_event event;

    event.events = mask;
    event.data.u32 = event_data;
    epoll_ctl(self->epoll_fd, EPOLL_CTL_MOD, fd, &event);
}

void EpollRegistry_SyncSlot(
    EpollRegistry *self,
    uint8_t slot,
    int32_t current_fd,
    uint32_t wanted_mask,
    uint32_t event_data
)
{
    struct epoll_event event;
    int32_t registered_fd;
    bool unchanged;

    if (slot >= EPOLL_REGISTRY_SLOTS_MAX) {
        return;
    }

    registered_fd = self->registered_fds[slot];
    unchanged = current_fd == registered_fd
                && (current_fd == EPOLL_REGISTRY_NO_SOCKET || wanted_mask == self->registered_masks[slot]);
    if (unchanged) {
        return;
    }

    if (registered_fd != EPOLL_REGISTRY_NO_SOCKET && registered_fd != current_fd) {
        epoll_ctl(self->epoll_fd, EPOLL_CTL_DEL, registered_fd, NULL);
        self->registered_fds[slot] = EPOLL_REGISTRY_NO_SOCKET;
    }
    if (current_fd == EPOLL_REGISTRY_NO_SOCKET) {
        self->registered_fds[slot] = EPOLL_REGISTRY_NO_SOCKET;
        return;
    }

    event.events = wanted_mask;
    event.data.u32 = event_data;
    if (registered_fd == current_fd) {
        epoll_ctl(self->epoll_fd, EPOLL_CTL_MOD, current_fd, &event);
    } else {
        epoll_ctl(self->epoll_fd, EPOLL_CTL_ADD, current_fd, &event);
    }
    self->registered_fds[slot] = current_fd;
    self->registered_masks[slot] = wanted_mask;
}

int32_t EpollRegistry_Wait(
    EpollRegistry *self,
    struct epoll_event *events,
    int32_t max_events,
    int32_t timeout_ms
)
{
    return (int32_t)epoll_wait(self->epoll_fd, events, max_events, timeout_ms);
}
