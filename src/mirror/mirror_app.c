#include "mirror/mirror_app.h"

#include <stdio.h>
#include <string.h>

#include "protocol/hello_message.h"
#include "protocol/list_message.h"
#include "protocol/message_header.h"
#include "protocol/open_message.h"

#define WIRE_BUFFER_SIZE 128

#define OPEN_FLAGS_READ_ONLY OPEN_FLAG_SUPPRESS_OWN_ECHO
#define OPEN_FLAGS_READ_WRITE (OPEN_FLAG_SUPPRESS_OWN_ECHO | OPEN_FLAG_WANT_WRITE)

static void transportOnConnected(void *context);
static void transportOnDisconnected(void *context, uint64_t now_us);
static void transportOnControl(void *context, const uint8_t *data, size_t size, uint64_t now_us);
static void transportOnFrame(void *context, const uint8_t *data, size_t size);
static void handleListReply(MirrorApp *self, const uint8_t *body, uint16_t length);
static void handleOpenAck(MirrorApp *self, const OpenAckMessage *ack);
static void openResolvedInterface(MirrorApp *self);
static void sendHello(MirrorApp *self);
static void sendList(MirrorApp *self, uint16_t offset);
static void sendOpen(MirrorApp *self, uint8_t flags);

/* ---------- public ---------- */

void MirrorApp_Init(MirrorApp *self, TransportPort *hub, CanPort *can, uint32_t interface_id, const char *interface_name)
{
    memset(self, 0, sizeof(*self));
    self->hub = hub;
    self->can = can;
    self->interface_id = interface_id;

    if (interface_name != NULL && interface_name[0] != '\0') {
        snprintf(self->interface_name, sizeof(self->interface_name), "%s", interface_name);
        self->state = kMIRROR_RESOLVING;
        return;
    }

    self->state = kMIRROR_OPENING;
}

TransportEvents MirrorApp_TransportEvents(MirrorApp *self)
{
    TransportEvents events = {
        .context = self,
        .on_connected = transportOnConnected,
        .on_disconnected = transportOnDisconnected,
        .on_control = transportOnControl,
        .on_frame = transportOnFrame,
    };

    return events;
}

void MirrorApp_OnCanFrame(MirrorApp *self, const FrameMessage *frame)
{
    FrameMessage outgoing = *frame;
    uint8_t encoded[WIRE_BUFFER_SIZE];
    size_t encoded_size;

    if (self->state != kMIRROR_PUMPING || !self->can_write) {
        return;
    }

    outgoing.channel = self->channel;
    outgoing.route_flags = 0;
    encoded_size = FrameMessage_Encode(&outgoing, encoded, sizeof(encoded));
    if (encoded_size > 0) {
        self->hub->send_frame(self->hub->context, encoded, encoded_size);
    }
}

uint8_t MirrorApp_State(const MirrorApp *self)
{
    return self->state;
}

bool MirrorApp_CanWrite(const MirrorApp *self)
{
    return self->can_write;
}

/* ---------- private ---------- */

static void transportOnConnected(void *context)
{
    MirrorApp *self = context;

    sendHello(self);
    if (self->state == kMIRROR_RESOLVING) {
        sendList(self, 0);
        return;
    }
    openResolvedInterface(self);
}

static void transportOnDisconnected(void *context, uint64_t now_us)
{
    MirrorApp *self = context;

    (void)now_us;

    self->state = kMIRROR_FAILED;
}

static void transportOnControl(void *context, const uint8_t *data, size_t size, uint64_t now_us)
{
    MirrorApp *self = context;
    MessageHeader header;
    OpenAckMessage ack;

    (void)now_us;

    if (!MessageHeader_Decode(&header, data, size)) {
        return;
    }
    if (size < (size_t)MESSAGE_HEADER_SIZE + header.length) {
        return;
    }

    if (header.type == kMESSAGE_TYPE_ERROR) {
        self->state = kMIRROR_FAILED;
        return;
    }
    if (header.type == kMESSAGE_TYPE_LIST_REPLY) {
        handleListReply(self, data + MESSAGE_HEADER_SIZE, header.length);
        return;
    }
    if (header.type == kMESSAGE_TYPE_OPEN_ACK) {
        if (OpenAckMessage_Decode(&ack, data + MESSAGE_HEADER_SIZE, header.length)) {
            handleOpenAck(self, &ack);
        }
    }
}

static void transportOnFrame(void *context, const uint8_t *data, size_t size)
{
    MirrorApp *self = context;
    MessageHeader header;
    FrameMessage frame;

    if (self->state != kMIRROR_PUMPING) {
        return;
    }
    if (!MessageHeader_Decode(&header, data, size)) {
        return;
    }
    if (header.type != kMESSAGE_TYPE_FRAME || size < (size_t)MESSAGE_HEADER_SIZE + header.length) {
        return;
    }
    if (!FrameMessage_Decode(&frame, data + MESSAGE_HEADER_SIZE, header.length)) {
        return;
    }

    self->can->write_frame(self->can->context, 0, &frame);
}

static void handleListReply(MirrorApp *self, const uint8_t *body, uint16_t length)
{
    ListReplyMessage reply;
    uint32_t interface_id;

    if (self->state != kMIRROR_RESOLVING) {
        return;
    }
    if (!ListReplyMessage_Decode(&reply, body, length)) {
        self->state = kMIRROR_FAILED;
        return;
    }

    if (InterfaceName_Find(&reply, self->interface_name, &interface_id)) {
        self->interface_id = interface_id;
        openResolvedInterface(self);
        return;
    }

    if ((reply.flags & LIST_REPLY_FLAG_MORE) != 0 && reply.count > 0) {
        self->list_offset = (uint16_t)(self->list_offset + reply.count);
        sendList(self, self->list_offset);
        return;
    }

    self->state = kMIRROR_FAILED;
}

static void openResolvedInterface(MirrorApp *self)
{
    self->state = kMIRROR_OPENING;
    self->pending_write = true;
    sendOpen(self, OPEN_FLAGS_READ_WRITE);
}

static void handleOpenAck(MirrorApp *self, const OpenAckMessage *ack)
{
    if (self->state != kMIRROR_OPENING || ack->interface_id != self->interface_id) {
        return;
    }

    if (ack->status == OPEN_STATUS_OK) {
        self->channel = ack->channel;
        self->can_write = self->pending_write;
        self->state = kMIRROR_PUMPING;
        return;
    }

    if (self->pending_write && ack->status == OPEN_STATUS_WRITE_DENIED) {
        self->pending_write = false;
        sendOpen(self, OPEN_FLAGS_READ_ONLY);
        return;
    }

    self->state = kMIRROR_FAILED;
}

static void sendHello(MirrorApp *self)
{
    HelloMessage hello = { PROTOCOL_VERSION, kPEER_ROLE_CLIENT, 0, "" };
    uint8_t encoded[WIRE_BUFFER_SIZE];
    size_t encoded_size = HelloMessage_Encode(&hello, encoded, sizeof(encoded));

    if (encoded_size > 0) {
        self->hub->send_control(self->hub->context, encoded, encoded_size);
    }
}

static void sendList(MirrorApp *self, uint16_t offset)
{
    ListMessage list = { offset };
    uint8_t encoded[WIRE_BUFFER_SIZE];
    size_t encoded_size = ListMessage_Encode(&list, encoded, sizeof(encoded));

    if (encoded_size > 0) {
        self->hub->send_control(self->hub->context, encoded, encoded_size);
    }
}

static void sendOpen(MirrorApp *self, uint8_t flags)
{
    OpenMessage open = { self->interface_id, flags };
    uint8_t encoded[WIRE_BUFFER_SIZE];
    size_t encoded_size = OpenMessage_Encode(&open, encoded, sizeof(encoded));

    if (encoded_size > 0) {
        self->hub->send_control(self->hub->context, encoded, encoded_size);
    }
}
