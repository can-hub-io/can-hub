#include "mirror/mirror_app.h"

#include <string.h>

#include "protocol/control_buffer.h"
#include "protocol/hello_message.h"
#include "protocol/list_message.h"
#include "protocol/message_header.h"
#include "protocol/open_message.h"
#include "shared/bounded_string.h"

#define OPEN_FLAGS_READ_ONLY (OPEN_FLAG_SUPPRESS_OWN_ECHO | OPEN_FLAG_ERROR_FRAMES)
#define OPEN_FLAGS_READ_WRITE (OPEN_FLAGS_READ_ONLY | OPEN_FLAG_WANT_WRITE)
#define MICROSECONDS_PER_MILLISECOND 1000

static void transportOnConnected(void *context);
static void transportOnDisconnected(void *context, uint64_t now_us);
static void transportOnControl(void *context, const uint8_t *data, size_t size, uint64_t now_us);
static void transportOnFrame(void *context, const uint8_t *data, size_t size);
static void tryConnect(MirrorApp *self, uint64_t now_us);
static void startResolution(MirrorApp *self);
static void awaitInterface(MirrorApp *self, uint64_t now_us);
static void handleListReply(MirrorApp *self, const uint8_t *body, uint16_t length, uint64_t now_us);
static void handleOpenAck(MirrorApp *self, const OpenAckMessage *ack, uint64_t now_us);
static void openResolvedInterface(MirrorApp *self);
static uint8_t openFlags(const MirrorApp *self, uint8_t access_flags);
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
    self->state = kMIRROR_DISCONNECTED;
    ReconnectBackoff_Init(&self->backoff, RECONNECT_DEFAULT_INITIAL_DELAY_MS, RECONNECT_DEFAULT_MAX_DELAY_MS);

    if (interface_name != NULL && interface_name[0] != '\0') {
        BoundedString_Copy(self->interface_name, sizeof(self->interface_name), interface_name);
        self->resolve_by_name = true;
    }
}

void MirrorApp_SetName(MirrorApp *self, const char *name)
{
    BoundedString_Copy(self->name, sizeof(self->name), name);
}

void MirrorApp_SetReliable(MirrorApp *self, bool reliable)
{
    self->reliable = reliable;
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

void MirrorApp_Tick(MirrorApp *self, uint64_t now_us)
{
    if (now_us < self->next_attempt_at_us) {
        return;
    }
    if (self->state == kMIRROR_DISCONNECTED) {
        tryConnect(self, now_us);
        return;
    }
    if (self->state == kMIRROR_AWAITING_INTERFACE) {
        startResolution(self);
    }
}

void MirrorApp_OnCanFrame(MirrorApp *self, const FrameMessage *frame)
{
    FrameMessage outgoing = *frame;
    uint8_t encoded[CONTROL_MESSAGE_MAX_WIRE_SIZE];
    size_t encoded_size;

    if (self->state != kMIRROR_PUMPING || !self->can_write) {
        return;
    }

    outgoing.channel = self->channel;
    outgoing.route_flags = 0;
    encoded_size = FrameMessage_Encode(&outgoing, encoded, sizeof(encoded));
    if (encoded_size > 0) {
        self->hub->send_frame(self->hub->context, outgoing.channel, encoded, encoded_size);
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

uint32_t MirrorApp_PendingReconnectDelayMs(const MirrorApp *self)
{
    return self->pending_reconnect_delay_ms;
}

/* ---------- private ---------- */

static void transportOnConnected(void *context)
{
    MirrorApp *self = context;

    sendHello(self);
    startResolution(self);
}

static void transportOnDisconnected(void *context, uint64_t now_us)
{
    MirrorApp *self = context;

    if (self->state == kMIRROR_DISCONNECTED || self->state == kMIRROR_FAILED) {
        return;
    }
    if (self->state != kMIRROR_CONNECTING && !self->resolve_by_name) {
        self->state = kMIRROR_FAILED;
        return;
    }

    self->can_write = false;
    self->state = kMIRROR_DISCONNECTED;
    self->pending_reconnect_delay_ms = ReconnectBackoff_NextDelayMs(&self->backoff);
    self->next_attempt_at_us = now_us + (uint64_t)self->pending_reconnect_delay_ms * MICROSECONDS_PER_MILLISECOND;
}

static void transportOnControl(void *context, const uint8_t *data, size_t size, uint64_t now_us)
{
    MirrorApp *self = context;
    MessageHeader header;
    OpenAckMessage ack;

    if (!MessageHeader_Decode(&header, data, size)) {
        return;
    }
    if (size < (size_t)MESSAGE_HEADER_SIZE + header.length) {
        return;
    }

    if (header.type == kMESSAGE_TYPE_LIST_REPLY) {
        handleListReply(self, data + MESSAGE_HEADER_SIZE, header.length, now_us);
        return;
    }
    if (header.type == kMESSAGE_TYPE_OPEN_ACK) {
        if (OpenAckMessage_Decode(&ack, data + MESSAGE_HEADER_SIZE, header.length)) {
            handleOpenAck(self, &ack, now_us);
        }
    }
}

static void transportOnFrame(void *context, const uint8_t *data, size_t size)
{
    MirrorApp *self = context;
    FrameStream stream;
    FrameMessage frame;

    if (self->state != kMIRROR_PUMPING) {
        return;
    }

    FrameStream_Init(&stream, data, size);
    while (FrameStream_Next(&stream, &frame)) {
        self->can->write_frame(self->can->context, 0, &frame);
    }
}

static void tryConnect(MirrorApp *self, uint64_t now_us)
{
    self->state = kMIRROR_CONNECTING;
    if (self->hub->connect(self->hub->context)) {
        return;
    }

    transportOnDisconnected(self, now_us);
}

static void startResolution(MirrorApp *self)
{
    if (!self->resolve_by_name) {
        openResolvedInterface(self);
        return;
    }

    self->state = kMIRROR_RESOLVING;
    self->list_offset = 0;
    sendList(self, 0);
}

static void awaitInterface(MirrorApp *self, uint64_t now_us)
{
    self->state = kMIRROR_AWAITING_INTERFACE;
    self->next_attempt_at_us = now_us + (uint64_t)MIRROR_INTERFACE_RETRY_MS * MICROSECONDS_PER_MILLISECOND;
}

static void handleListReply(MirrorApp *self, const uint8_t *body, uint16_t length, uint64_t now_us)
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

    awaitInterface(self, now_us);
}

static void openResolvedInterface(MirrorApp *self)
{
    self->state = kMIRROR_OPENING;
    self->pending_write = true;
    sendOpen(self, openFlags(self, OPEN_FLAGS_READ_WRITE));
}

static uint8_t openFlags(const MirrorApp *self, uint8_t access_flags)
{
    if (self->reliable) {
        return access_flags | OPEN_FLAG_RELIABLE;
    }

    return access_flags;
}

static void handleOpenAck(MirrorApp *self, const OpenAckMessage *ack, uint64_t now_us)
{
    if (self->state != kMIRROR_OPENING || ack->interface_id != self->interface_id) {
        return;
    }

    if (ack->status == OPEN_STATUS_OK) {
        self->channel = ack->channel;
        self->can_write = self->pending_write;
        self->state = kMIRROR_PUMPING;
        ReconnectBackoff_Reset(&self->backoff);
        if (self->reliable) {
            self->hub->set_channel_mode(self->hub->context, self->channel, true);
        }
        return;
    }

    if (self->pending_write && ack->status == OPEN_STATUS_WRITE_DENIED) {
        self->pending_write = false;
        sendOpen(self, openFlags(self, OPEN_FLAGS_READ_ONLY));
        return;
    }
    if (ack->status == OPEN_STATUS_REJECTED && self->resolve_by_name) {
        awaitInterface(self, now_us);
        return;
    }

    self->state = kMIRROR_FAILED;
}

static void sendHello(MirrorApp *self)
{
    uint8_t encoded[CONTROL_MESSAGE_MAX_WIRE_SIZE];
    size_t encoded_size;

    encoded_size = HelloMessage_Build(kPEER_ROLE_CLIENT, self->name, HELLO_CAP_RELIABLE_CHANNELS, encoded, sizeof(encoded));
    if (encoded_size > 0) {
        self->hub->send_control(self->hub->context, encoded, encoded_size);
    }
}

static void sendList(MirrorApp *self, uint16_t offset)
{
    ListMessage list = { offset };
    uint8_t encoded[CONTROL_MESSAGE_MAX_WIRE_SIZE];
    size_t encoded_size = ListMessage_Encode(&list, encoded, sizeof(encoded));

    if (encoded_size > 0) {
        self->hub->send_control(self->hub->context, encoded, encoded_size);
    }
}

static void sendOpen(MirrorApp *self, uint8_t flags)
{
    OpenMessage open = { self->interface_id, flags };
    uint8_t encoded[CONTROL_MESSAGE_MAX_WIRE_SIZE];
    size_t encoded_size = OpenMessage_Encode(&open, encoded, sizeof(encoded));

    if (encoded_size > 0) {
        self->hub->send_control(self->hub->context, encoded, encoded_size);
    }
}
