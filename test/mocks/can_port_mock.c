#include "can_port_mock.h"

#include <string.h>

static bool mockWriteFrame(void *context, uint8_t interface_index, const FrameMessage *frame);

/* ---------- public ---------- */

void CanPortMock_Reset(CanPortMock *self)
{
    memset(self, 0, sizeof(*self));
    self->port.context = self;
    self->port.write_frame = mockWriteFrame;
    self->write_result = true;
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
