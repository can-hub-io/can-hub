#include "client/client.h"

#include <string.h>

#include "protocol/control_buffer.h"
#include "protocol/hello_message.h"
#include "protocol/interface_status_message.h"
#include "protocol/message_header.h"
#include "protocol/open_message.h"
#include "protocol/txn_message.h"
#include "protocol/txn_steps_message.h"
#include "shared/bounded_string.h"

#define CLIENT_PACE_BURST_BITS 8192
#define FRAME_PACE_OVERHEAD_BITS 64
#define FRAME_PACE_BITS_PER_BYTE 10

typedef enum tclient_pending_e {
    kCLIENT_PENDING_NONE = 0,
    kCLIENT_PENDING_LIST,
    kCLIENT_PENDING_OPEN_ID,
    kCLIENT_PENDING_OPEN_NAME,
    kCLIENT_PENDING_MAX,
} TCLIENT_PENDING;

static void transportOnConnected(void *context);
static void transportOnDisconnected(void *context, uint64_t now_us);
static void transportOnControl(void *context, const uint8_t *data, size_t size, uint64_t now_us);
static void transportOnFrame(void *context, const uint8_t *data, size_t size);
static void runPending(Client *self);
static void startList(Client *self, uint16_t offset);
static void startOpenById(Client *self);
static void startOpenByName(Client *self);
static void handleError(Client *self, const uint8_t *body, uint16_t length);
static void handleListReply(Client *self, const uint8_t *body, uint16_t length);
static void handleOpenAck(Client *self, const uint8_t *body, uint16_t length);
static void handleInterfaceStatus(Client *self, const uint8_t *body, uint16_t length, uint64_t now_us);
static void handleConfigureReply(Client *self, const uint8_t *body, uint16_t length);
static uint64_t frameWireBits(const FrameMessage *frame);
static void resolveFromReply(Client *self, const ListReplyMessage *reply);
static void emitLocalError(Client *self, uint16_t code, const char *detail);
static void sendHello(Client *self);
static void sendList(Client *self, uint16_t offset);
static void sendOpen(Client *self);
static void sendSubscribe(Client *self);
static void sendConfigure(Client *self);
static void handleTxnBeginAck(Client *self, const uint8_t *body, uint16_t length);
static void handleTxnProgress(Client *self, const uint8_t *body, uint16_t length);
static void handleTxnStatus(Client *self, const uint8_t *body, uint16_t length);
static bool isTransactionRunning(const Client *self);

/* ---------- public ---------- */

void Client_Init(Client *self, TransportPort *hub, const ClientEvents *events)
{
    memset(self, 0, sizeof(*self));
    self->hub = hub;
    self->events = *events;
    self->state = kCLIENT_DISCONNECTED;
    self->pending = kCLIENT_PENDING_NONE;
}

void Client_SetName(Client *self, const char *name)
{
    BoundedString_Copy(self->name, sizeof(self->name), name);
}

TransportEvents Client_TransportEvents(Client *self)
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

void Client_RequestList(Client *self, uint16_t offset)
{
    if (!self->connected) {
        self->list_offset = offset;
        self->pending = kCLIENT_PENDING_LIST;
        return;
    }

    startList(self, offset);
}

void Client_OpenById(Client *self, uint32_t interface_id, uint8_t flags)
{
    self->interface_id = interface_id;
    self->open_flags = flags;

    if (!self->connected) {
        self->pending = kCLIENT_PENDING_OPEN_ID;
        return;
    }

    startOpenById(self);
}

void Client_OpenByName(Client *self, const char *interface_name, uint8_t flags)
{
    BoundedString_Copy(self->interface_name, sizeof(self->interface_name), interface_name);
    self->open_flags = flags;

    if (!self->connected) {
        self->pending = kCLIENT_PENDING_OPEN_NAME;
        return;
    }

    startOpenByName(self);
}

bool Client_Configure(Client *self, const char *interface_name, uint8_t op, const InterfaceSettings *settings)
{
    memset(&self->configure_request, 0, sizeof(self->configure_request));
    if (!InterfaceName_Split(interface_name, self->configure_request.agent_name, self->configure_request.interface_name)) {
        return false;
    }

    self->configure_request.op = op;
    self->configure_request.settings = *settings;

    if (!self->connected) {
        self->configure_pending = true;
        return true;
    }

    sendConfigure(self);

    return true;
}

void Client_SetFilters(Client *self, const CanFilter *filters, uint8_t count)
{
    if (count > SUBSCRIBE_FILTERS_MAX) {
        return;
    }

    memcpy(self->filters, filters, (size_t)count * sizeof(*filters));
    self->filter_count = count;

    if (self->state == kCLIENT_OPEN) {
        sendSubscribe(self);
    }
}

TCLIENT_SEND_RESULT Client_SendFrame(Client *self, FrameMessage *frame, uint64_t now_us)
{
    uint8_t encoded[FRAME_WIRE_SIZE];
    size_t encoded_size;

    if (self->state != kCLIENT_OPEN) {
        return kCLIENT_SEND_FAILED;
    }
    if (self->interface_leased) {
        return kCLIENT_SEND_LEASED;
    }

    frame->channel = self->channel;

    EgressShaper_Refill(&self->shaper, now_us);
    if (!EgressShaper_TryConsume(&self->shaper, frameWireBits(frame))) {
        self->frames_paced_dropped++;
        return kCLIENT_SEND_RATE_LIMITED;
    }

    encoded_size = FrameMessage_Encode(frame, encoded, sizeof(encoded));
    if (encoded_size == 0) {
        return kCLIENT_SEND_FAILED;
    }

    if (!self->hub->send_frame(self->hub->context, frame->channel, encoded, encoded_size)) {
        return kCLIENT_SEND_FAILED;
    }

    return kCLIENT_SEND_SENT;
}

uint64_t Client_FramesPacedDropped(const Client *self)
{
    return self->frames_paced_dropped;
}

uint8_t Client_State(const Client *self)
{
    return self->state;
}

uint8_t Client_Channel(const Client *self)
{
    return self->channel;
}

bool Client_InterfaceLeased(const Client *self)
{
    return self->interface_leased;
}

bool Client_BeginTransaction(Client *self, TxnBeginMessage *begin)
{
    uint8_t encoded[CONTROL_MESSAGE_MAX_WIRE_SIZE];
    size_t encoded_size;

    if (self->state != kCLIENT_OPEN) {
        return false;
    }
    if (self->transaction.state == kTRANSACTION_SENDER_BEGINNING || self->transaction.state == kTRANSACTION_SENDER_RUNNING) {
        return false;
    }

    begin->channel = self->channel;
    encoded_size = TxnBeginMessage_Encode(begin, encoded, sizeof(encoded));
    if (encoded_size == 0 || !self->hub->send_control(self->hub->context, encoded, encoded_size)) {
        return false;
    }

    TransactionSender_Begin(&self->transaction, self->channel);

    return true;
}

bool Client_DefineTemplate(Client *self, TxnTemplateMessage *template_message)
{
    uint8_t encoded[CONTROL_MESSAGE_MAX_WIRE_SIZE];
    size_t encoded_size;

    if (!isTransactionRunning(self) || !Client_FlushTransaction(self)) {
        return false;
    }

    template_message->channel = self->channel;
    template_message->txn_id = self->transaction.txn_id;
    encoded_size = TxnTemplateMessage_Encode(template_message, encoded, sizeof(encoded));
    if (encoded_size == 0) {
        return false;
    }

    return self->hub->send_frame(self->hub->context, self->channel, encoded, encoded_size);
}

uint32_t Client_SendSteps(Client *self, const TxnStep *steps, uint32_t count, TTRANSACTION_OFFER *stopped_by)
{
    TTRANSACTION_OFFER result = kTRANSACTION_OFFER_ACCEPTED;
    uint32_t accepted = 0;

    while (accepted < count) {
        result = TransactionSender_Offer(&self->transaction, &steps[accepted]);
        if (result == kTRANSACTION_OFFER_ACCEPTED) {
            accepted++;
            continue;
        }
        if (result != kTRANSACTION_OFFER_FLUSH_NEEDED || !Client_FlushTransaction(self)) {
            break;
        }
    }
    Client_FlushTransaction(self);

    *stopped_by = result;

    return accepted;
}

TTRANSACTION_OFFER Client_EndTransaction(Client *self)
{
    TTRANSACTION_OFFER result = TransactionSender_End(&self->transaction);

    if (result == kTRANSACTION_OFFER_FLUSH_NEEDED && Client_FlushTransaction(self)) {
        result = TransactionSender_End(&self->transaction);
    }
    Client_FlushTransaction(self);

    return result;
}

bool Client_FlushTransaction(Client *self)
{
    const uint8_t *batch;
    size_t batch_size = TransactionSender_PendingBatch(&self->transaction, &batch);

    if (batch_size == 0) {
        return true;
    }
    if (!self->hub->send_frame(self->hub->context, self->channel, batch, batch_size)) {
        return false;
    }

    TransactionSender_BatchSent(&self->transaction);

    return true;
}

void Client_AbortTransaction(Client *self)
{
    TxnAbortMessage abort_message;
    uint8_t encoded[CONTROL_MESSAGE_MAX_WIRE_SIZE];
    size_t encoded_size;

    if (!isTransactionRunning(self)) {
        return;
    }

    abort_message.channel = self->channel;
    abort_message.txn_id = self->transaction.txn_id;
    encoded_size = TxnAbortMessage_Encode(&abort_message, encoded, sizeof(encoded));
    self->hub->send_control(self->hub->context, encoded, encoded_size);
}

const TransactionSender *Client_Transaction(const Client *self)
{
    return &self->transaction;
}

/* ---------- private ---------- */

static void transportOnConnected(void *context)
{
    Client *self = context;

    self->connected = true;
    self->state = kCLIENT_READY;
    sendHello(self);
    runPending(self);
}

static void transportOnDisconnected(void *context, uint64_t now_us)
{
    Client *self = context;

    (void)now_us;

    self->connected = false;
    self->state = kCLIENT_DISCONNECTED;
    self->events.on_disconnected(self->events.context);
}

static void transportOnControl(void *context, const uint8_t *data, size_t size, uint64_t now_us)
{
    Client *self = context;
    MessageHeader header;

    if (!MessageHeader_Decode(&header, data, size)) {
        return;
    }
    if (size < (size_t)MESSAGE_HEADER_SIZE + header.length) {
        return;
    }

    if (header.type == kMESSAGE_TYPE_ERROR) {
        handleError(self, data + MESSAGE_HEADER_SIZE, header.length);
        return;
    }
    if (header.type == kMESSAGE_TYPE_LIST_REPLY) {
        handleListReply(self, data + MESSAGE_HEADER_SIZE, header.length);
        return;
    }
    if (header.type == kMESSAGE_TYPE_OPEN_ACK) {
        handleOpenAck(self, data + MESSAGE_HEADER_SIZE, header.length);
        return;
    }
    if (header.type == kMESSAGE_TYPE_INTERFACE_STATUS) {
        handleInterfaceStatus(self, data + MESSAGE_HEADER_SIZE, header.length, now_us);
        return;
    }
    if (header.type == kMESSAGE_TYPE_CONFIGURE_REPLY) {
        handleConfigureReply(self, data + MESSAGE_HEADER_SIZE, header.length);
        return;
    }
    if (header.type == kMESSAGE_TYPE_TXN_BEGIN_ACK) {
        handleTxnBeginAck(self, data + MESSAGE_HEADER_SIZE, header.length);
        return;
    }
    if (header.type == kMESSAGE_TYPE_TXN_PROGRESS) {
        handleTxnProgress(self, data + MESSAGE_HEADER_SIZE, header.length);
        return;
    }
    if (header.type == kMESSAGE_TYPE_TXN_STATUS) {
        handleTxnStatus(self, data + MESSAGE_HEADER_SIZE, header.length);
    }
}

static void transportOnFrame(void *context, const uint8_t *data, size_t size)
{
    Client *self = context;
    FrameStream stream;
    FrameMessage frame;

    if (self->state != kCLIENT_OPEN) {
        return;
    }

    FrameStream_Init(&stream, data, size);
    while (FrameStream_Next(&stream, &frame)) {
        self->events.on_frame(self->events.context, &frame);
    }
}

static void runPending(Client *self)
{
    uint8_t pending = self->pending;

    self->pending = kCLIENT_PENDING_NONE;

    if (self->configure_pending) {
        self->configure_pending = false;
        sendConfigure(self);
    }

    if (pending == kCLIENT_PENDING_LIST) {
        startList(self, self->list_offset);
        return;
    }
    if (pending == kCLIENT_PENDING_OPEN_ID) {
        startOpenById(self);
        return;
    }
    if (pending == kCLIENT_PENDING_OPEN_NAME) {
        startOpenByName(self);
    }
}

static void startList(Client *self, uint16_t offset)
{
    self->state = kCLIENT_LISTING;
    self->list_offset = offset;
    sendList(self, offset);
}

static void startOpenById(Client *self)
{
    self->state = kCLIENT_OPENING;
    sendOpen(self);
}

static void startOpenByName(Client *self)
{
    self->state = kCLIENT_RESOLVING;
    self->list_offset = 0;
    sendList(self, 0);
}

static void handleError(Client *self, const uint8_t *body, uint16_t length)
{
    ErrorMessage error;

    if (!ErrorMessage_Decode(&error, body, length)) {
        return;
    }

    self->events.on_error(self->events.context, error.code, error.detail);
}

static void handleListReply(Client *self, const uint8_t *body, uint16_t length)
{
    ListReplyMessage reply;

    if (self->state != kCLIENT_LISTING && self->state != kCLIENT_RESOLVING) {
        return;
    }

    if (!ListReplyMessage_Decode(&reply, body, length)) {
        emitLocalError(self, CLIENT_ERROR_MALFORMED_REPLY, "malformed list reply");
        return;
    }

    if (self->state == kCLIENT_RESOLVING) {
        resolveFromReply(self, &reply);
        return;
    }

    self->events.on_list_reply(self->events.context, &reply);
}

static void resolveFromReply(Client *self, const ListReplyMessage *reply)
{
    uint32_t interface_id;

    if (InterfaceName_Find(reply, self->interface_name, &interface_id)) {
        self->interface_id = interface_id;
        startOpenById(self);
        return;
    }

    if ((reply->flags & LIST_REPLY_FLAG_MORE) != 0 && reply->count > 0) {
        self->list_offset = (uint16_t)(self->list_offset + reply->count);
        sendList(self, self->list_offset);
        return;
    }

    emitLocalError(self, CLIENT_ERROR_INTERFACE_NOT_FOUND, self->interface_name);
}

static void handleOpenAck(Client *self, const uint8_t *body, uint16_t length)
{
    OpenAckMessage ack;

    if (self->state != kCLIENT_OPENING) {
        return;
    }
    if (!OpenAckMessage_Decode(&ack, body, length)) {
        return;
    }

    if (ack.status != OPEN_STATUS_OK) {
        self->state = kCLIENT_READY;
        self->events.on_open_result(self->events.context, ack.status, ack.channel, ack.interface_id);
        return;
    }

    self->channel = ack.channel;
    self->state = kCLIENT_OPEN;
    if (self->open_flags & OPEN_FLAG_RELIABLE) {
        self->hub->set_channel_mode(self->hub->context, self->channel, true);
    }
    if (self->filter_count > 0) {
        sendSubscribe(self);
    }
    self->events.on_open_result(self->events.context, ack.status, ack.channel, ack.interface_id);
}

static void handleInterfaceStatus(Client *self, const uint8_t *body, uint16_t length, uint64_t now_us)
{
    InterfaceStatusMessage status;
    uint8_t i;

    if (!InterfaceStatusMessage_Decode(&status, body, length)) {
        return;
    }

    for(i=0; i<status.interface_count; i++) {
        if (status.entries[i].channel != self->channel) {
            continue;
        }
        self->interface_leased = (status.entries[i].flags & INTERFACE_STATUS_FLAG_LEASED) != 0;
        if (self->shaper.burst_bits == 0) {
            EgressShaper_Init(&self->shaper, now_us, status.entries[i].advertised_rate, CLIENT_PACE_BURST_BITS);
        } else {
            EgressShaper_SetRate(&self->shaper, status.entries[i].advertised_rate);
        }
        return;
    }
}

static void handleConfigureReply(Client *self, const uint8_t *body, uint16_t length)
{
    ConfigureReplyMessage reply;

    if (self->events.on_configure_reply == NULL) {
        return;
    }
    if (!ConfigureReplyMessage_Decode(&reply, body, length)) {
        emitLocalError(self, CLIENT_ERROR_MALFORMED_REPLY, "malformed configure reply");
        return;
    }

    self->events.on_configure_reply(self->events.context, &reply);
}

static uint64_t frameWireBits(const FrameMessage *frame)
{
    return FRAME_PACE_OVERHEAD_BITS + (uint64_t)FRAME_PACE_BITS_PER_BYTE * frame->payload_length;
}

static void emitLocalError(Client *self, uint16_t code, const char *detail)
{
    self->state = kCLIENT_READY;
    self->events.on_error(self->events.context, code, detail);
}

static void sendHello(Client *self)
{
    uint8_t encoded[CONTROL_MESSAGE_MAX_WIRE_SIZE];
    size_t encoded_size;

    encoded_size = HelloMessage_Build(kPEER_ROLE_CLIENT, self->name, HELLO_CAP_RELIABLE_CHANNELS, encoded, sizeof(encoded));

    if (encoded_size > 0) {
        self->hub->send_control(self->hub->context, encoded, encoded_size);
    }
}

static void sendList(Client *self, uint16_t offset)
{
    ListMessage list = { offset };
    uint8_t encoded[CONTROL_MESSAGE_MAX_WIRE_SIZE];
    size_t encoded_size = ListMessage_Encode(&list, encoded, sizeof(encoded));

    if (encoded_size > 0) {
        self->hub->send_control(self->hub->context, encoded, encoded_size);
    }
}

static void sendOpen(Client *self)
{
    OpenMessage open = { self->interface_id, self->open_flags };
    uint8_t encoded[CONTROL_MESSAGE_MAX_WIRE_SIZE];
    size_t encoded_size = OpenMessage_Encode(&open, encoded, sizeof(encoded));

    if (encoded_size > 0) {
        self->hub->send_control(self->hub->context, encoded, encoded_size);
    }
}

static void sendSubscribe(Client *self)
{
    SubscribeMessage subscribe;
    uint8_t encoded[SUBSCRIBE_WIRE_SIZE];
    size_t encoded_size;

    subscribe.channel = self->channel;
    subscribe.filter_count = self->filter_count;
    memcpy(subscribe.filters, self->filters, (size_t)self->filter_count * sizeof(*self->filters));
    encoded_size = SubscribeMessage_Encode(&subscribe, encoded, sizeof(encoded));

    if (encoded_size > 0) {
        self->hub->send_control(self->hub->context, encoded, encoded_size);
    }
}

static void sendConfigure(Client *self)
{
    uint8_t encoded[CONTROL_MESSAGE_MAX_WIRE_SIZE];
    size_t encoded_size = ConfigureMessage_Encode(&self->configure_request, encoded, sizeof(encoded));

    if (encoded_size > 0) {
        self->hub->send_control(self->hub->context, encoded, encoded_size);
    }
}

static void handleTxnBeginAck(Client *self, const uint8_t *body, uint16_t length)
{
    TxnBeginAckMessage ack;

    if (TxnBeginAckMessage_Decode(&ack, body, length)) {
        TransactionSender_OnBeginAck(&self->transaction, &ack);
    }
}

static void handleTxnProgress(Client *self, const uint8_t *body, uint16_t length)
{
    TxnProgressMessage progress;

    if (TxnProgressMessage_Decode(&progress, body, length)) {
        TransactionSender_OnProgress(&self->transaction, &progress);
    }
}

static void handleTxnStatus(Client *self, const uint8_t *body, uint16_t length)
{
    TxnStatusMessage status;

    if (TxnStatusMessage_Decode(&status, body, length)) {
        TransactionSender_OnStatus(&self->transaction, &status);
    }
}

static bool isTransactionRunning(const Client *self)
{
    return self->transaction.state == kTRANSACTION_SENDER_RUNNING;
}
