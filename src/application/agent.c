#include "application/agent.h"

#include "protocol/hello_message.h"
#include "protocol/message_header.h"

#define CONTROL_BUFFER_SIZE 512
#define MICROSECONDS_PER_MILLISECOND 1000
#define PING_REPLY_FLAG 0x01

typedef void (*TControlHandler)(Agent *self, const MessageHeader *header, const uint8_t *payload, uint64_t now_us);

static void eventConnected(void *context);
static void eventDisconnected(void *context, uint64_t now_us);
static void eventControl(void *context, const uint8_t *data, size_t size, uint64_t now_us);
static void eventTransportFrame(void *context, const uint8_t *data, size_t size);
static void eventCanFrame(void *context, uint8_t interface_index, const FrameMessage *frame);
static void tryConnect(Agent *self, uint64_t now_us);
static void scheduleReconnect(Agent *self, uint64_t now_us);
static void sendHelloAndRegister(Agent *self);
static void handleRegisterAck(Agent *self, const MessageHeader *header, const uint8_t *payload, uint64_t now_us);
static void handlePing(Agent *self, const MessageHeader *header, const uint8_t *payload, uint64_t now_us);

static const TControlHandler control_handlers[kMESSAGE_TYPE_MAX] = {
    [kMESSAGE_TYPE_REGISTER_ACK] = handleRegisterAck,
    [kMESSAGE_TYPE_PING] = handlePing,
};

/* ---------- public ---------- */

void Agent_Init(Agent *self, TransportPort *transport, CanPort *can, const RegisterMessage *registration)
{
    self->transport = transport;
    self->can = can;
    self->registration = *registration;
    ChannelMap_Reset(&self->channel_map);
    ReconnectBackoff_Init(&self->backoff, RECONNECT_DEFAULT_INITIAL_DELAY_MS, RECONNECT_DEFAULT_MAX_DELAY_MS);
    self->state = kAGENT_STATE_DISCONNECTED;
    self->next_connect_at_us = 0;
}

TransportEvents Agent_TransportEvents(Agent *self)
{
    TransportEvents events = {
        .context = self,
        .on_connected = eventConnected,
        .on_disconnected = eventDisconnected,
        .on_control = eventControl,
        .on_frame = eventTransportFrame,
    };

    return events;
}

CanEvents Agent_CanEvents(Agent *self)
{
    CanEvents events = {
        .context = self,
        .on_frame = eventCanFrame,
    };

    return events;
}

void Agent_Tick(Agent *self, uint64_t now_us)
{
    if (self->state != kAGENT_STATE_DISCONNECTED) {
        return;
    }
    if (now_us < self->next_connect_at_us) {
        return;
    }

    tryConnect(self, now_us);
}

void Agent_OnConnected(Agent *self)
{
    if (self->state != kAGENT_STATE_CONNECTING) {
        return;
    }

    sendHelloAndRegister(self);
    self->state = kAGENT_STATE_REGISTERING;
}

void Agent_OnDisconnected(Agent *self, uint64_t now_us)
{
    if (self->state == kAGENT_STATE_DISCONNECTED) {
        return;
    }

    ChannelMap_Reset(&self->channel_map);
    self->state = kAGENT_STATE_DISCONNECTED;
    scheduleReconnect(self, now_us);
}

void Agent_OnControlMessage(Agent *self, const uint8_t *data, size_t size, uint64_t now_us)
{
    MessageHeader header;

    if (!MessageHeader_Decode(&header, data, size)) {
        return;
    }
    if (size < (size_t)MESSAGE_HEADER_SIZE + header.length) {
        return;
    }
    if (header.type >= kMESSAGE_TYPE_MAX || control_handlers[header.type] == NULL) {
        return;
    }

    control_handlers[header.type](self, &header, data + MESSAGE_HEADER_SIZE, now_us);
}

void Agent_OnCanFrame(Agent *self, uint8_t interface_index, const FrameMessage *frame)
{
    FrameMessage outgoing = *frame;
    uint8_t encoded[MESSAGE_HEADER_SIZE + FRAME_FIXED_FIELDS_SIZE + FRAME_PAYLOAD_MAX_FD];
    size_t encoded_size;

    if (self->state != kAGENT_STATE_RUNNING) {
        return;
    }
    if (!ChannelMap_ChannelForInterface(&self->channel_map, interface_index, &outgoing.channel)) {
        return;
    }

    encoded_size = FrameMessage_Encode(&outgoing, encoded, sizeof(encoded));
    if (encoded_size == 0) {
        return;
    }

    self->transport->send_frame(self->transport->context, encoded, encoded_size);
}

void Agent_OnTransportFrame(Agent *self, const uint8_t *data, size_t size)
{
    MessageHeader header;
    FrameMessage frame;
    uint8_t interface_index;

    if (self->state != kAGENT_STATE_RUNNING) {
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
    if (!ChannelMap_InterfaceForChannel(&self->channel_map, frame.channel, &interface_index)) {
        return;
    }

    self->can->write_frame(self->can->context, interface_index, &frame);
}

uint8_t Agent_State(const Agent *self)
{
    return self->state;
}

/* ---------- private ---------- */

static void eventConnected(void *context)
{
    Agent_OnConnected(context);
}

static void eventDisconnected(void *context, uint64_t now_us)
{
    Agent_OnDisconnected(context, now_us);
}

static void eventControl(void *context, const uint8_t *data, size_t size, uint64_t now_us)
{
    Agent_OnControlMessage(context, data, size, now_us);
}

static void eventTransportFrame(void *context, const uint8_t *data, size_t size)
{
    Agent_OnTransportFrame(context, data, size);
}

static void eventCanFrame(void *context, uint8_t interface_index, const FrameMessage *frame)
{
    Agent_OnCanFrame(context, interface_index, frame);
}

static void tryConnect(Agent *self, uint64_t now_us)
{
    if (self->transport->connect(self->transport->context)) {
        self->state = kAGENT_STATE_CONNECTING;
        return;
    }

    scheduleReconnect(self, now_us);
}

static void scheduleReconnect(Agent *self, uint64_t now_us)
{
    uint64_t delay_ms = ReconnectBackoff_NextDelayMs(&self->backoff);

    self->next_connect_at_us = now_us + delay_ms * MICROSECONDS_PER_MILLISECOND;
}

static void sendHelloAndRegister(Agent *self)
{
    HelloMessage hello = { PROTOCOL_VERSION, kPEER_ROLE_AGENT, 0 };
    uint8_t encoded[CONTROL_BUFFER_SIZE];
    size_t encoded_size;

    encoded_size = HelloMessage_Encode(&hello, encoded, sizeof(encoded));
    self->transport->send_control(self->transport->context, encoded, encoded_size);

    encoded_size = RegisterMessage_Encode(&self->registration, encoded, sizeof(encoded));
    self->transport->send_control(self->transport->context, encoded, encoded_size);
}

static void handleRegisterAck(Agent *self, const MessageHeader *header, const uint8_t *payload, uint64_t now_us)
{
    RegisterAckMessage ack;
    bool ack_accepted;

    if (self->state != kAGENT_STATE_REGISTERING) {
        return;
    }

    ack_accepted = RegisterAckMessage_Decode(&ack, payload, header->length)
                   && ChannelMap_AssignFromAck(&self->channel_map, &ack);
    if (!ack_accepted) {
        self->transport->disconnect(self->transport->context);
        Agent_OnDisconnected(self, now_us);
        return;
    }

    ReconnectBackoff_Reset(&self->backoff);
    self->state = kAGENT_STATE_RUNNING;
}

static void handlePing(Agent *self, const MessageHeader *header, const uint8_t *payload, uint64_t now_us)
{
    MessageHeader pong;
    uint8_t encoded[MESSAGE_HEADER_SIZE];

    (void)payload;
    (void)now_us;

    if (header->flags & PING_REPLY_FLAG) {
        return;
    }

    pong.type = kMESSAGE_TYPE_PING;
    pong.flags = PING_REPLY_FLAG;
    pong.length = 0;
    MessageHeader_Encode(&pong, encoded, sizeof(encoded));
    self->transport->send_control(self->transport->context, encoded, sizeof(encoded));
}
