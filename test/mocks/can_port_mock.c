#include "can_port_mock.h"

#include <string.h>

static bool mockWriteFrame(void *context, uint8_t interface_index, const FrameMessage *frame);
static bool mockConfigure(void *context, uint8_t interface_index, const InterfaceSettings *requested, char *detail);
static bool mockQuery(void *context, uint8_t interface_index, InterfaceSettings *effective);
static uint32_t mockBitrate(void *context, uint8_t interface_index);

/* ---------- public ---------- */

void CanPortMock_Reset(CanPortMock *self)
{
    memset(self, 0, sizeof(*self));
    self->port.context = self;
    self->port.write_frame = mockWriteFrame;
    self->port.configure = mockConfigure;
    self->port.query = mockQuery;
    self->port.bitrate = mockBitrate;
    self->write_result = true;
    self->configure_result = true;
    self->query_result = true;
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

static bool mockConfigure(void *context, uint8_t interface_index, const InterfaceSettings *requested, char *detail)
{
    CanPortMock *self = context;

    self->configure_count++;
    self->last_configure_interface_index = interface_index;
    self->last_configure_settings = *requested;
    memcpy(detail, self->configure_detail, IFCONFIG_DETAIL_SIZE);

    return self->configure_result;
}

static bool mockQuery(void *context, uint8_t interface_index, InterfaceSettings *effective)
{
    CanPortMock *self = context;

    (void)interface_index;

    self->query_count++;
    *effective = self->query_settings;

    return self->query_result;
}

static uint32_t mockBitrate(void *context, uint8_t interface_index)
{
    CanPortMock *self = context;

    (void)interface_index;

    return self->bitrate_value;
}
