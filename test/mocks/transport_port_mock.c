#include "transport_port_mock.h"

#include <string.h>

static bool mockConnect(void *context);
static void mockDisconnect(void *context);
static bool mockSendControl(void *context, const uint8_t *data, size_t size);
static bool mockSendFrame(void *context, uint8_t channel, const uint8_t *data, size_t size);
static void mockSetChannelMode(void *context, uint8_t channel, bool reliable);

/* ---------- public ---------- */

void TransportPortMock_Reset(TransportPortMock *self)
{
    memset(self, 0, sizeof(*self));
    self->port.context = self;
    self->port.connect = mockConnect;
    self->port.disconnect = mockDisconnect;
    self->port.send_control = mockSendControl;
    self->port.send_frame = mockSendFrame;
    self->port.set_channel_mode = mockSetChannelMode;
    self->connect_result = true;
}

/* ---------- private ---------- */

static bool mockConnect(void *context)
{
    TransportPortMock *self = context;

    self->connect_calls++;

    return self->connect_result;
}

static void mockDisconnect(void *context)
{
    TransportPortMock *self = context;

    self->disconnect_calls++;
}

static bool mockSendControl(void *context, const uint8_t *data, size_t size)
{
    TransportPortMock *self = context;

    if (self->control_count >= MOCK_CONTROL_LOG_MESSAGES_MAX || size > MOCK_CONTROL_LOG_SIZE) {
        return false;
    }

    memcpy(self->control_log[self->control_count], data, size);
    self->control_sizes[self->control_count] = size;
    self->control_count++;

    return true;
}

static bool mockSendFrame(void *context, uint8_t channel, const uint8_t *data, size_t size)
{
    TransportPortMock *self = context;

    if (size > MOCK_FRAME_BUFFER_SIZE) {
        return false;
    }

    memcpy(self->last_frame, data, size);
    self->last_frame_size = size;
    self->last_frame_channel = channel;
    self->frame_count++;

    return true;
}

static void mockSetChannelMode(void *context, uint8_t channel, bool reliable)
{
    TransportPortMock *self = context;

    self->channel_mode_count++;
    self->last_channel_mode_channel = channel;
    self->last_channel_mode_reliable = reliable;
}
