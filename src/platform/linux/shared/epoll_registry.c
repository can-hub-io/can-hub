#include "platform/linux/shared/epoll_registry.h"

#include <stddef.h>

#define SOLE_OCCUPANT 0

static void vacateSlot(EpollRegistry *self, uint16_t slot);
static bool isRegisteredByAnotherSlot(const EpollRegistry *self, uint16_t slot, int32_t fd);

/* ---------- public ---------- */

bool EpollRegistry_Open(EpollRegistry *self)
{
    uint16_t slot;

    self->epoll_fd = epoll_create1(0);
    for(slot=0; slot<EPOLL_REGISTRY_SLOTS_MAX; slot++) {
        self->registered_fds[slot] = EPOLL_REGISTRY_NO_SOCKET;
        self->registered_masks[slot] = 0;
        self->registered_occupants[slot] = SOLE_OCCUPANT;
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
    uint16_t slot,
    int32_t current_fd,
    uint32_t wanted_mask,
    uint32_t event_data
)
{
    EpollRegistry_SyncOccupiedSlot(self, slot, SOLE_OCCUPANT, current_fd, wanted_mask, event_data);
}

void EpollRegistry_SyncOccupiedSlot(
    EpollRegistry *self,
    uint16_t slot,
    uint32_t occupant,
    int32_t current_fd,
    uint32_t wanted_mask,
    uint32_t event_data
)
{
    struct epoll_event event;
    bool same_socket;

    if (slot >= EPOLL_REGISTRY_SLOTS_MAX) {
        return;
    }

    same_socket = current_fd == self->registered_fds[slot] && occupant == self->registered_occupants[slot];
    if (same_socket && (current_fd == EPOLL_REGISTRY_NO_SOCKET || wanted_mask == self->registered_masks[slot])) {
        return;
    }
    if (!same_socket) {
        vacateSlot(self, slot);
    }
    if (current_fd == EPOLL_REGISTRY_NO_SOCKET) {
        return;
    }

    event.events = wanted_mask;
    event.data.u32 = event_data;
    if (same_socket) {
        epoll_ctl(self->epoll_fd, EPOLL_CTL_MOD, current_fd, &event);
    } else {
        epoll_ctl(self->epoll_fd, EPOLL_CTL_ADD, current_fd, &event);
    }
    self->registered_fds[slot] = current_fd;
    self->registered_masks[slot] = wanted_mask;
    self->registered_occupants[slot] = occupant;
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

/* ---------- private ---------- */

static void vacateSlot(EpollRegistry *self, uint16_t slot)
{
    int32_t registered_fd = self->registered_fds[slot];

    if (registered_fd == EPOLL_REGISTRY_NO_SOCKET) {
        return;
    }

    self->registered_fds[slot] = EPOLL_REGISTRY_NO_SOCKET;
    if (!isRegisteredByAnotherSlot(self, slot, registered_fd)) {
        epoll_ctl(self->epoll_fd, EPOLL_CTL_DEL, registered_fd, NULL);
    }
}

static bool isRegisteredByAnotherSlot(const EpollRegistry *self, uint16_t slot, int32_t fd)
{
    uint16_t other;

    for(other=0; other<EPOLL_REGISTRY_SLOTS_MAX; other++) {
        if (other != slot && self->registered_fds[other] == fd) {
            return true;
        }
    }

    return false;
}
