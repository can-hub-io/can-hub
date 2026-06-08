#include "can_port_mock.h"

#include <string.h>

static bool mockWriteFrame(void *context, uint8_t interface_index, const FrameMessage *frame);
static bool mockConfigure(void *context, uint8_t interface_index, uint8_t op, uint32_t bitrate);

/* ---------- public ---------- */

void CanPortMock_Reset(CanPortMock *self)
{
    memset(self, 0, sizeof(*self));
    self->port.context = self;
    self->port.write_frame = mockWriteFrame;
    self->port.configure = mockConfigure;
    self->write_result = true;
    self->configure_result = true;
}

/* ---------- private ---------- */

static bool mockWriteFrame(void *context, uint8_t interface_index, const FrameMessage *frame)
{
    CanPortMock *self = context;

    self->write_count++;
    self->last_interface_index = interface_index;
    self->last_frame = *frame;

    return self->write_result;
}

static bool mockConfigure(void *context, uint8_t interface_index, uint8_t op, uint32_t bitrate)
{
    CanPortMock *self = context;

    self->configure_count++;
    self->last_configure_interface_index = interface_index;
    self->last_configure_op = op;
    self->last_configure_bitrate = bitrate;

    return self->configure_result;
}
