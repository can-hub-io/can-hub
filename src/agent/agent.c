#include "agent/agent.h"

#include <string.h>

#include "protocol/control_buffer.h"
#include "protocol/hello_message.h"
#include "protocol/ifconfig_message.h"
#include "protocol/interface_status_message.h"
#include "protocol/message_header.h"
#include "protocol/txn_message.h"
#include "protocol/txn_steps_message.h"
#include "shared/bounded_string.h"

#define MICROSECONDS_PER_MILLISECOND 1000
#define PING_REPLY_FLAG 0x01
#define AGENT_CAPABILITIES (HELLO_CAP_RELIABLE_CHANNELS | HELLO_CAP_TRANSACTIONS | HELLO_CAP_ISOTP)
#define TIMING_CONFLICT_DETAIL "give a bitrate or a time quantum and segments, not both"

typedef void (*TControlHandler)(Agent *self, const MessageHeader *header, const uint8_t *payload, uint64_t now_us);

static void eventConnected(void *context);
static void injectFrame(Agent *self, const FrameMessage *frame);
static void eventDisconnected(void *context, uint64_t now_us);
static void eventControl(void *context, const uint8_t *data, size_t size, uint64_t now_us);
static void eventTransportFrame(void *context, const uint8_t *data, size_t size);
static bool eventCanFrame(void *context, uint8_t interface_index, const FrameMessage *frame);
static void tryConnect(Agent *self, uint64_t now_us);
static void tickRegistering(Agent *self, uint64_t now_us);
static void tickRunning(Agent *self, uint64_t now_us);
static void scheduleReconnect(Agent *self, uint64_t now_us);
static void sendHelloAndRegister(Agent *self);
static void sendInterfaceStatus(Agent *self);
static void handleRegisterAck(Agent *self, const MessageHeader *header, const uint8_t *payload, uint64_t now_us);
static void handlePing(Agent *self, const MessageHeader *header, const uint8_t *payload, uint64_t now_us);
static void handleIfconfig(Agent *self, const MessageHeader *header, const uint8_t *payload, uint64_t now_us);
static uint8_t applyIfconfig(Agent *self, const IfconfigMessage *request, uint8_t interface_index, char *detail);
static bool interfaceIndexForName(const Agent *self, const char *interface_name, uint8_t *interface_index);
static void handleTxnBegin(Agent *self, const MessageHeader *header, const uint8_t *payload, uint64_t now_us);
static void handleTxnAbort(Agent *self, const MessageHeader *header, const uint8_t *payload, uint64_t now_us);
static void handleTxnRoster(Agent *self, const MessageHeader *header, const uint8_t *payload, uint64_t now_us);
static void handleTxnTemplate(Agent *self, const MessageHeader *header, const uint8_t *payload, uint64_t now_us);
static void handleTxnSteps(Agent *self, const MessageHeader *header, const uint8_t *payload, uint64_t now_us);
static bool isTransactionStreamMessage(uint8_t type);
static void dispatchTransactionStreamMessage(Agent *self, const uint8_t *data, size_t size);
static TransactionRunnerPort transactionRunnerPort(Agent *self);
static bool transmitTransactionFrame(void *context, uint8_t interface_index, const FrameMessage *frame);
static void reportTransactionProgress(void *context, const TxnProgressMessage *progress);
static void reportTransactionStatus(void *context, const TxnStatusMessage *status);
static void reportTransactionRoster(void *context, const TxnRosterReportMessage *report);
static void sendEncodedControl(Agent *self, const uint8_t *encoded, size_t encoded_size);

static const TControlHandler control_handlers[kMESSAGE_TYPE_MAX] = {
    [kMESSAGE_TYPE_REGISTER_ACK] = handleRegisterAck,
    [kMESSAGE_TYPE_PING] = handlePing,
    [kMESSAGE_TYPE_IFCONFIG] = handleIfconfig,
    [kMESSAGE_TYPE_TXN_BEGIN] = handleTxnBegin,
    [kMESSAGE_TYPE_TXN_ABORT] = handleTxnAbort,
    [kMESSAGE_TYPE_TXN_ROSTER] = handleTxnRoster,
    [kMESSAGE_TYPE_TXN_TEMPLATE] = handleTxnTemplate,
    [kMESSAGE_TYPE_TXN_STEPS] = handleTxnSteps,
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
    TransactionRunner_Init(&self->transactions, transactionRunnerPort(self), self->transaction_ring, TXN_RING_BYTES);
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
    TransactionRunner_Cancel(&self->transactions);
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

bool Agent_OnCanFrame(Agent *self, uint8_t interface_index, const FrameMessage *frame)
{
    FrameMessage outgoing = *frame;
    uint8_t encoded[MESSAGE_HEADER_SIZE + FRAME_FIXED_FIELDS_SIZE + FRAME_PAYLOAD_MAX_FD];
    size_t encoded_size;
    uint8_t token;
    bool accepted;

    if (self->state != kAGENT_STATE_RUNNING) {
        return true;
    }
    if (!ChannelMap_ChannelForInterface(&self->channel_map, interface_index, &outgoing.channel)) {
        return true;
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
        return true;
    }

    accepted = self->transport->send_frame(self->transport->context, outgoing.channel, encoded, encoded_size);
    if (accepted) {
        TransactionRunner_OnFrame(&self->transactions, interface_index, frame);
    }

    return accepted;
}

void Agent_OnTransportFrame(Agent *self, const uint8_t *data, size_t size)
{
    FrameStream stream;
    FrameMessage frame;
    MessageHeader header;

    if (self->state != kAGENT_STATE_RUNNING) {
        return;
    }
    if (MessageHeader_Decode(&header, data, size) && isTransactionStreamMessage(header.type)) {
        dispatchTransactionStreamMessage(self, data, size);
        return;
    }

    FrameStream_Init(&stream, data, size);
    while (FrameStream_Next(&stream, &frame)) {
        injectFrame(self, &frame);
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

uint64_t Agent_NextWakeUs(const Agent *self)
{
    if (self->state != kAGENT_STATE_RUNNING) {
        return 0;
    }
    return TransactionRunner_NextWakeUs(&self->transactions);
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

static bool eventCanFrame(void *context, uint8_t interface_index, const FrameMessage *frame)
{
    return Agent_OnCanFrame(context, interface_index, frame);
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
    TransactionRunner_Tick(&self->transactions, now_us);
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

    encoded_size = HelloMessage_Build(kPEER_ROLE_AGENT, NULL, AGENT_CAPABILITIES, encoded, sizeof(encoded));
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
    if (!ChannelMap_AssignFromAck(&self->channel_map, &ack, self->registration.interface_count)) {
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

    memset(&reply, 0, sizeof(reply));
    reply.request_id = request.request_id;

    if (!interfaceIndexForName(self, request.interface_name, &interface_index)) {
        reply.status = IFCONFIG_STATUS_UNKNOWN_INTERFACE;
    } else {
        reply.status = applyIfconfig(self, &request, interface_index, reply.detail);
        self->can->query(self->can->context, interface_index, &reply.settings);
    }

    encoded_size = IfconfigReplyMessage_Encode(&reply, encoded, sizeof(encoded));
    if (encoded_size > 0) {
        self->transport->send_control(self->transport->context, encoded, encoded_size);
    }
}

static uint8_t applyIfconfig(Agent *self, const IfconfigMessage *request, uint8_t interface_index, char *detail)
{
    if (request->op == IFCONFIG_OP_QUERY) {
        return IFCONFIG_STATUS_OK;
    }
    if (request->op != IFCONFIG_OP_APPLY) {
        return IFCONFIG_STATUS_APPLY_FAILED;
    }
    if (InterfaceSettings_HasTimingConflict(&request->settings)) {
        BoundedString_Copy(detail, IFCONFIG_DETAIL_SIZE, TIMING_CONFLICT_DETAIL);
        return IFCONFIG_STATUS_APPLY_FAILED;
    }
    if (!self->can->configure(self->can->context, interface_index, &request->settings, detail)) {
        return IFCONFIG_STATUS_APPLY_FAILED;
    }

    return IFCONFIG_STATUS_OK;
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

static void injectFrame(Agent *self, const FrameMessage *frame)
{
    uint8_t interface_index;
    uint8_t token;

    if (!ChannelMap_InterfaceForChannel(&self->channel_map, frame->channel, &interface_index)) {
        return;
    }

    token = (uint8_t)((frame->route_flags & FRAME_ROUTE_TOKEN_MASK) >> FRAME_ROUTE_TOKEN_SHIFT);
    EchoCorrelator_Push(&self->echo, interface_index, token, frame->can_id);
    if (!self->can->write_frame(self->can->context, interface_index, frame)) {
        EchoCorrelator_DropNewest(&self->echo, interface_index);
        self->tx_dropped[interface_index]++;
    }
}

static void handleTxnBegin(Agent *self, const MessageHeader *header, const uint8_t *payload, uint64_t now_us)
{
    TxnBeginMessage begin;
    TxnBeginAckMessage ack;
    uint8_t encoded[MESSAGE_HEADER_SIZE + TXN_BEGIN_ACK_BODY_SIZE];
    uint8_t interface_index;

    if (self->state != kAGENT_STATE_RUNNING) {
        return;
    }
    if (!TxnBeginMessage_Decode(&begin, payload, header->length)) {
        return;
    }

    if (ChannelMap_InterfaceForChannel(&self->channel_map, begin.channel, &interface_index)) {
        TransactionRunner_Begin(&self->transactions, &begin, interface_index, now_us, &ack);
    } else {
        memset(&ack, 0, sizeof(ack));
        ack.channel = begin.channel;
        ack.status = kTXN_BEGIN_STATUS_INVALID;
    }

    sendEncodedControl(self, encoded, TxnBeginAckMessage_Encode(&ack, encoded, sizeof(encoded)));
}

static void handleTxnAbort(Agent *self, const MessageHeader *header, const uint8_t *payload, uint64_t now_us)
{
    TxnAbortMessage abort_message;

    (void)now_us;

    if (!TxnAbortMessage_Decode(&abort_message, payload, header->length)) {
        return;
    }

    TransactionRunner_Abort(&self->transactions, abort_message.txn_id);
}

static void handleTxnRoster(Agent *self, const MessageHeader *header, const uint8_t *payload, uint64_t now_us)
{
    TxnRosterMessage roster;

    (void)now_us;

    if (!TxnRosterMessage_Decode(&roster, payload, header->length)) {
        return;
    }

    TransactionRunner_DefineRoster(&self->transactions, &roster);
}

static void handleTxnTemplate(Agent *self, const MessageHeader *header, const uint8_t *payload, uint64_t now_us)
{
    TxnTemplateMessage template_message;

    (void)now_us;

    if (!TxnTemplateMessage_Decode(&template_message, payload, header->length)) {
        return;
    }

    TransactionRunner_DefineTemplate(&self->transactions, &template_message);
}

static void handleTxnSteps(Agent *self, const MessageHeader *header, const uint8_t *payload, uint64_t now_us)
{
    TxnStepsMessage steps;

    (void)now_us;

    if (!TxnStepsMessage_Decode(&steps, payload, header->length)) {
        return;
    }

    TransactionRunner_PushSteps(&self->transactions, &steps);
}

static bool isTransactionStreamMessage(uint8_t type)
{
    return type == kMESSAGE_TYPE_TXN_ROSTER || type == kMESSAGE_TYPE_TXN_TEMPLATE || type == kMESSAGE_TYPE_TXN_STEPS;
}

static void dispatchTransactionStreamMessage(Agent *self, const uint8_t *data, size_t size)
{
    MessageHeader header;

    MessageHeader_Decode(&header, data, size);
    if (size < (size_t)MESSAGE_HEADER_SIZE + header.length) {
        return;
    }

    control_handlers[header.type](self, &header, data + MESSAGE_HEADER_SIZE, 0);
}

static TransactionRunnerPort transactionRunnerPort(Agent *self)
{
    TransactionRunnerPort port = {
        .context = self,
        .transmit = transmitTransactionFrame,
        .report_progress = reportTransactionProgress,
        .report_status = reportTransactionStatus,
        .report_roster = reportTransactionRoster,
    };

    return port;
}

static bool transmitTransactionFrame(void *context, uint8_t interface_index, const FrameMessage *frame)
{
    Agent *self = context;

    EchoCorrelator_Push(&self->echo, interface_index, FRAME_ROUTE_NO_TOKEN, frame->can_id);
    if (self->can->write_frame(self->can->context, interface_index, frame)) {
        return true;
    }

    EchoCorrelator_DropNewest(&self->echo, interface_index);

    return false;
}

static void reportTransactionProgress(void *context, const TxnProgressMessage *progress)
{
    Agent *self = context;
    uint8_t encoded[MESSAGE_HEADER_SIZE + TXN_PROGRESS_BODY_SIZE];

    sendEncodedControl(self, encoded, TxnProgressMessage_Encode(progress, encoded, sizeof(encoded)));
}

static void reportTransactionStatus(void *context, const TxnStatusMessage *status)
{
    Agent *self = context;
    uint8_t encoded[MESSAGE_HEADER_SIZE + TXN_STATUS_BODY_SIZE];

    sendEncodedControl(self, encoded, TxnStatusMessage_Encode(status, encoded, sizeof(encoded)));
}

static void reportTransactionRoster(void *context, const TxnRosterReportMessage *report)
{
    Agent *self = context;
    uint8_t encoded[MESSAGE_HEADER_SIZE + TXN_ROSTER_REPORT_BODY_SIZE];

    sendEncodedControl(self, encoded, TxnRosterReportMessage_Encode(report, encoded, sizeof(encoded)));
}

static void sendEncodedControl(Agent *self, const uint8_t *encoded, size_t encoded_size)
{
    if (encoded_size == 0) {
        return;
    }

    self->transport->send_control(self->transport->context, encoded, encoded_size);
}
