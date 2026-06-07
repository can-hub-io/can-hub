#include "hub_transport_port_mock.h"

#include <string.h>

static bool mockSendControl(void *context, uint32_t peer_id, const uint8_t *data, size_t size);
static bool mockSendFrame(void *context, uint32_t peer_id, const uint8_t *data, size_t size);
static void mockClosePeer(void *context, uint32_t peer_id);

/* ---------- public ---------- */

void HubTransportPortMock_Reset(HubTransportPortMock *self)
{
    memset(self, 0, sizeof(*self));
    self->port.context = self;
    self->port.send_control = mockSendControl;
    self->port.send_frame = mockSendFrame;
    self->port.close_peer = mockClosePeer;
    self->control_result = true;
    self->frame_result = true;
}

/* ---------- private ---------- */

static bool mockSendControl(void *context, uint32_t peer_id, const uint8_t *data, size_t size)
{
    HubTransportPortMock *self = context;

    if (!self->control_result) {
        return false;
    }
    if (self->control_count >= HUB_MOCK_CONTROL_LOG_MAX || size > HUB_MOCK_CONTROL_SIZE) {
        return false;
    }

    self->control_peers[self->control_count] = peer_id;
    memcpy(self->control_log[self->control_count], data, size);
    self->control_sizes[self->control_count] = size;
    self->control_count++;

    return true;
}

static bool mockSendFrame(void *context, uint32_t peer_id, const uint8_t *data, size_t size)
{
    HubTransportPortMock *self = context;

    if (!self->frame_result) {
        return false;
    }
    if (self->frame_count >= HUB_MOCK_FRAME_LOG_MAX || size > HUB_MOCK_FRAME_SIZE) {
        return false;
    }

    self->frame_peers[self->frame_count] = peer_id;
    memcpy(self->frame_log[self->frame_count], data, size);
    self->frame_sizes[self->frame_count] = size;
    self->frame_count++;

    return true;
}

static void mockClosePeer(void *context, uint32_t peer_id)
{
    HubTransportPortMock *self = context;

    self->close_count++;
    self->last_closed_peer = peer_id;
}
