#include "agent/agent.h"

#include <string.h>

#include "protocol/control_buffer.h"
#include "protocol/hello_message.h"
#include "protocol/ifconfig_message.h"
#include "protocol/interface_status_message.h"
#include "protocol/message_header.h"

#define MICROSECONDS_PER_MILLISECOND 1000
#define PING_REPLY_FLAG 0x01

typedef void (*TControlHandler)(Agent *self, const MessageHeader *header, const uint8_t *payload, uint64_t now_us);

static void eventConnected(void *context);
static void eventDisconnected(void *context, uint64_t now_us);
static void eventControl(void *context, const uint8_t *data, size_t size, uint64_t now_us);
static void eventTransportFrame(void *context, const uint8_t *data, size_t size);
static void eventCanFrame(void *context, uint8_t interface_index, const FrameMessage *frame);
static void tryConnect(Agent *self, uint64_t now_us);
static void tickRegistering(Agent *self, uint64_t now_us);
static void tickRunning(Agent *self, uint64_t now_us);
static void scheduleReconnect(Agent *self, uint64_t now_us);
static void sendHelloAndRegister(Agent *self);
static void sendInterfaceStatus(Agent *self);
static void handleRegisterAck(Agent *self, const MessageHeader *header, const uint8_t *payload, uint64_t now_us);
static void handlePing(Agent *self, const MessageHeader *header, const uint8_t *payload, uint64_t now_us);
static void handleIfconfig(Agent *self, const MessageHeader *header, const uint8_t *payload, uint64_t now_us);
static bool interfaceIndexForName(const Agent *self, const char *interface_name, uint8_t *interface_index);

static const TControlHandler control_handlers[kMESSAGE_TYPE_MAX] = {
    [kMESSAGE_TYPE_REGISTER_ACK] = handleRegisterAck,
    [kMESSAGE_TYPE_PING] = handlePing,
    [kMESSAGE_TYPE_IFCONFIG] = handleIfconfig,
};

/* ---------- public ---------- */

void Agent_Init(Agent *self, TransportPort *transport, CanPort *can, const RegisterMessage *registration)
{
    self->transport = transport;
    self->can = can;
    self->registration = *registration;
    ChannelMap_Reset(&self->channel_map);
    EchoCorrelator_Reset(&self->echo);
    TxPacer_Reset(&self->tx_pacer);
    ReconnectBackoff_Init(&self->backoff, RECONNECT_DEFAULT_INITIAL_DELAY_MS, RECONNECT_DEFAULT_MAX_DELAY_MS);
    self->state = kAGENT_STATE_DISCONNECTED;
    self->next_connect_at_us = 0;
    self->register_deadline_us = 0;
    self->next_status_at_us = 0;
    self->pending_reconnect_delay_ms = 0;
    memset(self->tx_dropped, 0, sizeof(self->tx_dropped));
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
    if (self->state == kAGENT_STATE_REGISTERING) {
        tickRegistering(self, now_us);
        return;
    }
    if (self->state == kAGENT_STATE_RUNNING) {
        tickRunning(self, now_us);
        return;
    }
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
    EchoCorrelator_Reset(&self->echo);
    self->state = kAGENT_STATE_DISCONNECTED;
    self->register_deadline_us = 0;
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
    uint8_t token;

    if (self->state != kAGENT_STATE_RUNNING) {
        return;
    }
    if (!ChannelMap_ChannelForInterface(&self->channel_map, interface_index, &outgoing.channel)) {
        return;
    }

    if (frame->route_flags & FRAME_ROUTE_FLAG_ECHO) {
        outgoing.route_flags = FRAME_ROUTE_FLAG_ECHO;
        if (EchoCorrelator_PopMatch(&self->echo, interface_index, frame->can_id, &token)) {
            outgoing.route_flags |= (uint8_t)(token << FRAME_ROUTE_TOKEN_SHIFT);
        }
    } else {
        outgoing.route_flags = 0;
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
    uint8_t token;

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

    token = (uint8_t)((frame.route_flags & FRAME_ROUTE_TOKEN_MASK) >> FRAME_ROUTE_TOKEN_SHIFT);
    EchoCorrelator_Push(&self->echo, interface_index, token, frame.can_id);
    if (!self->can->write_frame(self->can->context, interface_index, &frame)) {
        EchoCorrelator_DropNewest(&self->echo, interface_index);
        self->tx_dropped[interface_index]++;
    }
}

uint8_t Agent_State(const Agent *self)
{
    return self->state;
}

uint32_t Agent_PendingReconnectDelayMs(const Agent *self)
{
    return self->pending_reconnect_delay_ms;
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

static void tickRegistering(Agent *self, uint64_t now_us)
{
    if (self->register_deadline_us == 0) {
        self->register_deadline_us = now_us + AGENT_REGISTER_TIMEOUT_MS * MICROSECONDS_PER_MILLISECOND;
        return;
    }
    if (now_us < self->register_deadline_us) {
        return;
    }

    self->transport->disconnect(self->transport->context);
    Agent_OnDisconnected(self, now_us);
}

static void tickRunning(Agent *self, uint64_t now_us)
{
    if (now_us < self->next_status_at_us) {
        return;
    }

    sendInterfaceStatus(self);
    self->next_status_at_us = now_us + AGENT_STATUS_PERIOD_MS * MICROSECONDS_PER_MILLISECOND;
}

static void scheduleReconnect(Agent *self, uint64_t now_us)
{
    uint64_t delay_ms = ReconnectBackoff_NextDelayMs(&self->backoff);

    self->pending_reconnect_delay_ms = (uint32_t)delay_ms;
    self->next_connect_at_us = now_us + delay_ms * MICROSECONDS_PER_MILLISECOND;
}

static void sendHelloAndRegister(Agent *self)
{
    uint8_t encoded[CONTROL_MESSAGE_MAX_WIRE_SIZE];
    size_t encoded_size;

    encoded_size = HelloMessage_Build(kPEER_ROLE_AGENT, NULL, 0, encoded, sizeof(encoded));
    self->transport->send_control(self->transport->context, encoded, encoded_size);

    encoded_size = RegisterMessage_Encode(&self->registration, encoded, sizeof(encoded));
    self->transport->send_control(self->transport->context, encoded, encoded_size);
}

static void sendInterfaceStatus(Agent *self)
{
    InterfaceStatusMessage status;
    uint8_t encoded[MESSAGE_HEADER_SIZE + INTERFACE_STATUS_BODY_SIZE];
    size_t encoded_size;
    uint32_t advertised_rate;
    uint8_t channel;
    uint8_t count = 0;
    uint8_t i;

    for(i=0; i<self->registration.interface_count; i++) {
        if (!ChannelMap_ChannelForInterface(&self->channel_map, i, &channel)) {
            continue;
        }
        advertised_rate = self->can->bitrate(self->can->context, i);
        status.entries[count].channel = channel;
        status.entries[count].flags = 0;
        status.entries[count].advertised_rate = advertised_rate;
        status.entries[count].credit = TxPacer_Update(&self->tx_pacer, i, advertised_rate, self->tx_dropped[i]);
        status.entries[count].tx_dropped = self->tx_dropped[i];
        count++;
    }
    status.interface_count = count;

    encoded_size = InterfaceStatusMessage_Encode(&status, encoded, sizeof(encoded));
    if (encoded_size == 0) {
        return;
    }

    self->transport->send_control(self->transport->context, encoded, encoded_size);
}

static void handleRegisterAck(Agent *self, const MessageHeader *header, const uint8_t *payload, uint64_t now_us)
{
    RegisterAckMessage ack;

    if (self->state != kAGENT_STATE_REGISTERING) {
        return;
    }

    if (!RegisterAckMessage_Decode(&ack, payload, header->length)) {
        self->transport->disconnect(self->transport->context);
        Agent_OnDisconnected(self, now_us);
        return;
    }
    if (ack.status != REGISTER_STATUS_OK) {
        self->transport->disconnect(self->transport->context);
        Agent_OnDisconnected(self, now_us);
        return;
    }
    if (!ChannelMap_AssignFromAck(&self->channel_map, &ack)) {
        self->transport->disconnect(self->transport->context);
        Agent_OnDisconnected(self, now_us);
        return;
    }

    ReconnectBackoff_Reset(&self->backoff);
    self->state = kAGENT_STATE_RUNNING;
    self->register_deadline_us = 0;
    sendInterfaceStatus(self);
    self->next_status_at_us = now_us + AGENT_STATUS_PERIOD_MS * MICROSECONDS_PER_MILLISECOND;
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

static void handleIfconfig(Agent *self, const MessageHeader *header, const uint8_t *payload, uint64_t now_us)
{
    IfconfigMessage request;
    IfconfigReplyMessage reply;
    uint8_t encoded[CONTROL_MESSAGE_MAX_WIRE_SIZE];
    uint8_t interface_index;
    size_t encoded_size;

    (void)now_us;

    if (self->state != kAGENT_STATE_RUNNING) {
        return;
    }
    if (!IfconfigMessage_Decode(&request, payload, header->length)) {
        return;
    }

    memset(reply.interface_name, 0, sizeof(reply.interface_name));
    memcpy(reply.interface_name, request.interface_name, sizeof(reply.interface_name));

    if (!interfaceIndexForName(self, request.interface_name, &interface_index)) {
        reply.status = IFCONFIG_STATUS_UNKNOWN_INTERFACE;
    } else if (self->can->configure(self->can->context, interface_index, request.op, request.bitrate)) {
        reply.status = IFCONFIG_STATUS_OK;
    } else {
        reply.status = IFCONFIG_STATUS_APPLY_FAILED;
    }

    encoded_size = IfconfigReplyMessage_Encode(&reply, encoded, sizeof(encoded));
    if (encoded_size > 0) {
        self->transport->send_control(self->transport->context, encoded, encoded_size);
    }
}

static bool interfaceIndexForName(const Agent *self, const char *interface_name, uint8_t *interface_index)
{
    uint8_t i;

    for(i=0; i<self->registration.interface_count; i++) {
        if (strcmp(self->registration.interface_names[i], interface_name) == 0) {
            *interface_index = i;
            return true;
        }
    }

    return false;
}
