#include "agent/domain/isotp_link.h"

#include <string.h>

#define PCI_TYPE_SHIFT 4
#define PCI_LOW_MASK 0x0F
#define PCI_SINGLE_FRAME 0x0
#define PCI_FIRST_FRAME 0x1
#define PCI_CONSECUTIVE_FRAME 0x2
#define PCI_FLOW_CONTROL 0x3
#define FLOW_STATUS_CONTINUE 0x0
#define FLOW_STATUS_WAIT 0x1
#define FLOW_STATUS_OVERFLOW 0x2
#define CLASSIC_FRAME_SIZE 8
#define SINGLE_FRAME_HEADER_SIZE 1
#define ESCAPE_SINGLE_FRAME_HEADER_SIZE 2
#define ESCAPE_SINGLE_FRAME_LENGTH_OFFSET 1
#define FIRST_FRAME_HEADER_SIZE 2
#define FIRST_FRAME_LENGTH_HIGH_SHIFT 8
#define FIRST_FRAME_LENGTH_LOW_MASK 0xFF
#define CONSECUTIVE_FRAME_HEADER_SIZE 1
#define FLOW_CONTROL_SIZE 3
#define FLOW_CONTROL_BLOCK_SIZE_OFFSET 1
#define FLOW_CONTROL_ST_MIN_OFFSET 2
#define FIRST_FRAME_LENGTH_LOW_OFFSET 1
#define SEQUENCE_MASK 0x0F
#define FIRST_SEQUENCE 1
#define ST_MIN_MILLISECONDS_MAX 0x7F
#define ST_MIN_MICROSECONDS_FIRST 0xF1
#define ST_MIN_MICROSECONDS_LAST 0xF9
#define ST_MIN_MICROSECONDS_BASE 0xF0
#define ST_MIN_MICROSECONDS_STEP 100
#define MICROSECONDS_PER_MILLISECOND 1000
#define ADDRESS_BYTE_SIZE 1

static const uint8_t FD_FRAME_LENGTHS[] = { 12, 16, 20, 24, 32, 48, 64 };

static uint8_t onFlowControl(IsotpLink *self, const FrameMessage *frame, uint64_t now_us);
static uint8_t onSingleFrame(IsotpLink *self, const FrameMessage *frame);
static uint8_t onFirstFrame(IsotpLink *self, const FrameMessage *frame, uint64_t now_us);
static uint8_t onConsecutiveFrame(IsotpLink *self, const FrameMessage *frame, uint64_t now_us);
static uint8_t tickReceiver(IsotpLink *self, uint64_t now_us);
static uint8_t tickSender(IsotpLink *self, uint64_t now_us);
static uint8_t sendFirstFrame(IsotpLink *self, uint64_t now_us);
static uint8_t sendSingleFrame(IsotpLink *self, uint64_t now_us);
static uint8_t sendSegmentedFirstFrame(IsotpLink *self, uint64_t now_us);
static uint8_t sendConsecutiveFrame(IsotpLink *self, uint64_t now_us);
static uint8_t retryLater(IsotpLink *self, uint64_t now_us);
static bool transmitFlowControl(IsotpLink *self, uint8_t flow_status);
static bool transmitData(IsotpLink *self, const uint8_t *data, uint8_t size);
static uint8_t frameLength(const IsotpLink *self, uint8_t size);
static bool isFromPeer(const IsotpLink *self, const FrameMessage *frame);
static uint8_t addressSize(const IsotpLink *self);
static uint8_t txDataLength(const IsotpLink *self);
static uint8_t classicSingleFrameCapacity(const IsotpLink *self);
static uint8_t escapeSingleFrameCapacity(const IsotpLink *self);
static uint8_t firstFrameCapacity(const IsotpLink *self);
static uint8_t consecutiveFrameCapacity(const IsotpLink *self);
static uint32_t stMinUs(uint8_t st_min);
static uint8_t fail(IsotpLink *self, uint8_t reason);
static uint64_t earliest(uint64_t current_us, uint64_t candidate_us);

/* ---------- public ---------- */

void IsotpLink_Init(IsotpLink *self, IsotpLinkPort port)
{
    memset(self, 0, sizeof(*self));
    self->port = port;
}

void IsotpLink_Open(IsotpLink *self, uint32_t request_id, uint8_t channel, const TxnIsotpParameters *parameters)
{
    self->request_id = request_id;
    self->channel = channel;
    self->parameters = *parameters;
    memset(&self->sender, 0, sizeof(self->sender));
    self->receiver.state = kISOTP_RECEIVER_IDLE;
    self->receiver.flow_control_pending = false;
    self->failure_reason = kTXN_REASON_NONE;
}

void IsotpLink_Send(IsotpLink *self, const uint8_t *message, uint16_t length, uint64_t now_us)
{
    memset(&self->sender, 0, sizeof(self->sender));
    self->sender.message = message;
    self->sender.length = length;
    self->sender.state = kISOTP_SENDER_FIRST_FRAME;
    self->sender.next_frame_us = now_us;
    self->sender.deadline_us = now_us + ISOTP_TIMEOUT_US;
}

uint8_t IsotpLink_OnFrame(IsotpLink *self, const FrameMessage *frame, uint64_t now_us)
{
    uint8_t pci_type;

    if (!isFromPeer(self, frame)) {
        return kISOTP_EVENT_NONE;
    }

    pci_type = frame->payload[addressSize(self)] >> PCI_TYPE_SHIFT;
    if (pci_type == PCI_FLOW_CONTROL) {
        return onFlowControl(self, frame, now_us);
    }
    if (pci_type == PCI_SINGLE_FRAME) {
        return onSingleFrame(self, frame);
    }
    if (pci_type == PCI_FIRST_FRAME) {
        return onFirstFrame(self, frame, now_us);
    }
    if (pci_type == PCI_CONSECUTIVE_FRAME) {
        return onConsecutiveFrame(self, frame, now_us);
    }
    return kISOTP_EVENT_NONE;
}

uint8_t IsotpLink_Tick(IsotpLink *self, uint64_t now_us)
{
    uint8_t event = tickReceiver(self, now_us);

    if (event != kISOTP_EVENT_NONE) {
        return event;
    }
    return tickSender(self, now_us);
}

uint64_t IsotpLink_NextWakeUs(const IsotpLink *self)
{
    uint64_t wake_us = 0;

    if (self->sender.state == kISOTP_SENDER_FIRST_FRAME || self->sender.state == kISOTP_SENDER_CONSECUTIVE_FRAMES) {
        wake_us = earliest(wake_us, self->sender.next_frame_us);
    }
    if (self->sender.state == kISOTP_SENDER_FLOW_CONTROL) {
        wake_us = earliest(wake_us, self->sender.deadline_us);
    }
    if (self->receiver.state != kISOTP_RECEIVER_CONSECUTIVE_FRAMES) {
        return wake_us;
    }
    if (self->receiver.flow_control_pending) {
        return earliest(wake_us, self->receiver.flow_control_due_us);
    }
    return earliest(wake_us, self->receiver.deadline_us);
}

uint8_t IsotpLink_FailureReason(const IsotpLink *self)
{
    return self->failure_reason;
}

void IsotpLink_MessageFrame(const IsotpLink *self, FrameMessage *frame)
{
    uint16_t length = self->receiver.length;

    if (length > FRAME_PAYLOAD_MAX_FD) {
        length = FRAME_PAYLOAD_MAX_FD;
    }

    memset(frame, 0, sizeof(*frame));
    frame->can_id = self->parameters.response_id;
    frame->channel = self->channel;
    frame->payload_length = (uint8_t)length;
    memcpy(frame->payload, self->receiver.message, length);
}

/* ---------- private ---------- */

static uint8_t onFlowControl(IsotpLink *self, const FrameMessage *frame, uint64_t now_us)
{
    IsotpSender *sender = &self->sender;
    const uint8_t *pdu = frame->payload + addressSize(self);
    uint8_t flow_status = pdu[0] & PCI_LOW_MASK;

    if (sender->state != kISOTP_SENDER_FLOW_CONTROL || frame->payload_length < addressSize(self) + FLOW_CONTROL_SIZE) {
        return kISOTP_EVENT_NONE;
    }
    if (flow_status == FLOW_STATUS_OVERFLOW) {
        return fail(self, kTXN_REASON_OVERFLOW);
    }
    if (flow_status == FLOW_STATUS_WAIT) {
        sender->wait_count++;
        if (sender->wait_count > ISOTP_WAIT_FRAMES_MAX) {
            return fail(self, kTXN_REASON_WAIT_LIMIT);
        }
        sender->deadline_us = now_us + ISOTP_TIMEOUT_US;
        return kISOTP_EVENT_NONE;
    }
    if (flow_status != FLOW_STATUS_CONTINUE) {
        return kISOTP_EVENT_NONE;
    }

    sender->wait_count = 0;
    sender->block_remaining = pdu[FLOW_CONTROL_BLOCK_SIZE_OFFSET];
    sender->block_limited = sender->block_remaining != 0;
    sender->st_min_us = stMinUs(pdu[FLOW_CONTROL_ST_MIN_OFFSET]);
    sender->state = kISOTP_SENDER_CONSECUTIVE_FRAMES;
    sender->next_frame_us = now_us;
    sender->deadline_us = now_us + ISOTP_TIMEOUT_US;

    return kISOTP_EVENT_NONE;
}

static uint8_t onSingleFrame(IsotpLink *self, const FrameMessage *frame)
{
    IsotpReceiver *receiver = &self->receiver;
    const uint8_t *pdu = frame->payload + addressSize(self);
    uint8_t pdu_size = (uint8_t)(frame->payload_length - addressSize(self));
    uint8_t header_size = SINGLE_FRAME_HEADER_SIZE;
    uint8_t length = pdu[0] & PCI_LOW_MASK;

    if (length == 0 && frame->payload_length > CLASSIC_FRAME_SIZE) {
        header_size = ESCAPE_SINGLE_FRAME_HEADER_SIZE;
        length = pdu[ESCAPE_SINGLE_FRAME_LENGTH_OFFSET];
    }
    if (length == 0 || pdu_size < header_size + length) {
        return kISOTP_EVENT_NONE;
    }

    memcpy(receiver->message, pdu + header_size, length);
    receiver->length = length;
    receiver->state = kISOTP_RECEIVER_IDLE;
    receiver->flow_control_pending = false;

    return kISOTP_EVENT_MESSAGE;
}

static uint8_t onFirstFrame(IsotpLink *self, const FrameMessage *frame, uint64_t now_us)
{
    IsotpReceiver *receiver = &self->receiver;
    const uint8_t *pdu = frame->payload + addressSize(self);
    uint8_t data_size;
    uint16_t high_bits;
    uint16_t length;

    if (frame->payload_length < CLASSIC_FRAME_SIZE) {
        return kISOTP_EVENT_NONE;
    }
    data_size = (uint8_t)(frame->payload_length - addressSize(self) - FIRST_FRAME_HEADER_SIZE);
    high_bits = (uint16_t)(pdu[0] & PCI_LOW_MASK) << FIRST_FRAME_LENGTH_HIGH_SHIFT;
    length = (uint16_t)(high_bits | pdu[FIRST_FRAME_LENGTH_LOW_OFFSET]);
    if (length == 0) {
        transmitFlowControl(self, FLOW_STATUS_OVERFLOW);
        return fail(self, kTXN_REASON_OVERFLOW);
    }
    if (length <= data_size) {
        return kISOTP_EVENT_NONE;
    }

    memcpy(receiver->message, pdu + FIRST_FRAME_HEADER_SIZE, data_size);
    receiver->length = length;
    receiver->received = data_size;
    receiver->sequence = FIRST_SEQUENCE;
    receiver->block_received = 0;
    receiver->state = kISOTP_RECEIVER_CONSECUTIVE_FRAMES;
    receiver->flow_control_pending = true;
    receiver->flow_control_due_us = now_us;

    return kISOTP_EVENT_NONE;
}

static uint8_t onConsecutiveFrame(IsotpLink *self, const FrameMessage *frame, uint64_t now_us)
{
    IsotpReceiver *receiver = &self->receiver;
    const uint8_t *pdu = frame->payload + addressSize(self);
    uint8_t carried = (uint8_t)(frame->payload_length - addressSize(self) - CONSECUTIVE_FRAME_HEADER_SIZE);
    uint16_t remaining = (uint16_t)(receiver->length - receiver->received);
    uint8_t size = remaining < carried ? (uint8_t)remaining : carried;

    if (receiver->state != kISOTP_RECEIVER_CONSECUTIVE_FRAMES || receiver->flow_control_pending) {
        return kISOTP_EVENT_NONE;
    }
    if ((pdu[0] & PCI_LOW_MASK) != receiver->sequence) {
        return fail(self, kTXN_REASON_SEQUENCE_ERROR);
    }
    if (size == 0) {
        return kISOTP_EVENT_NONE;
    }

    memcpy(receiver->message + receiver->received, pdu + CONSECUTIVE_FRAME_HEADER_SIZE, size);
    receiver->received = (uint16_t)(receiver->received + size);
    receiver->sequence = (uint8_t)((receiver->sequence + 1) & SEQUENCE_MASK);
    if (receiver->received == receiver->length) {
        receiver->state = kISOTP_RECEIVER_IDLE;
        return kISOTP_EVENT_MESSAGE;
    }

    receiver->deadline_us = now_us + ISOTP_TIMEOUT_US;
    receiver->block_received++;
    if (self->parameters.block_size != 0 && receiver->block_received == self->parameters.block_size) {
        receiver->block_received = 0;
        receiver->flow_control_pending = true;
        receiver->flow_control_due_us = now_us;
    }

    return kISOTP_EVENT_NONE;
}

static uint8_t tickReceiver(IsotpLink *self, uint64_t now_us)
{
    IsotpReceiver *receiver = &self->receiver;

    if (receiver->state != kISOTP_RECEIVER_CONSECUTIVE_FRAMES) {
        return kISOTP_EVENT_NONE;
    }
    if (!receiver->flow_control_pending) {
        return now_us >= receiver->deadline_us ? fail(self, kTXN_REASON_CONSECUTIVE_FRAME_TIMEOUT) : kISOTP_EVENT_NONE;
    }
    if (now_us < receiver->flow_control_due_us) {
        return kISOTP_EVENT_NONE;
    }

    if (!transmitFlowControl(self, FLOW_STATUS_CONTINUE)) {
        receiver->flow_control_due_us = now_us + ISOTP_RETRY_US;
        return kISOTP_EVENT_NONE;
    }
    receiver->flow_control_pending = false;
    receiver->deadline_us = now_us + ISOTP_TIMEOUT_US;

    return kISOTP_EVENT_NONE;
}

static uint8_t tickSender(IsotpLink *self, uint64_t now_us)
{
    IsotpSender *sender = &self->sender;
    uint8_t event = kISOTP_EVENT_NONE;
    uint8_t i;

    if (sender->state == kISOTP_SENDER_FLOW_CONTROL) {
        return now_us >= sender->deadline_us ? fail(self, kTXN_REASON_FLOW_CONTROL_TIMEOUT) : kISOTP_EVENT_NONE;
    }

    for(i=0; i<ISOTP_FRAMES_PER_TICK_MAX && event == kISOTP_EVENT_NONE; i++) {
        if (now_us < sender->next_frame_us) {
            break;
        }
        if (sender->state == kISOTP_SENDER_FIRST_FRAME) {
            event = sendFirstFrame(self, now_us);
        } else if (sender->state == kISOTP_SENDER_CONSECUTIVE_FRAMES) {
            event = sendConsecutiveFrame(self, now_us);
        } else {
            break;
        }
    }
    return event;
}

static uint8_t sendFirstFrame(IsotpLink *self, uint64_t now_us)
{
    if (self->sender.length <= classicSingleFrameCapacity(self) || self->sender.length <= escapeSingleFrameCapacity(self)) {
        return sendSingleFrame(self, now_us);
    }
    return sendSegmentedFirstFrame(self, now_us);
}

static uint8_t sendSingleFrame(IsotpLink *self, uint64_t now_us)
{
    IsotpSender *sender = &self->sender;
    uint8_t data[FRAME_PAYLOAD_MAX_FD];
    uint8_t header_size = SINGLE_FRAME_HEADER_SIZE;

    data[0] = (uint8_t)sender->length;
    if (sender->length > classicSingleFrameCapacity(self)) {
        header_size = ESCAPE_SINGLE_FRAME_HEADER_SIZE;
        data[0] = PCI_SINGLE_FRAME;
        data[ESCAPE_SINGLE_FRAME_LENGTH_OFFSET] = (uint8_t)sender->length;
    }
    memcpy(data + header_size, sender->message, sender->length);
    if (!transmitData(self, data, (uint8_t)(header_size + sender->length))) {
        return retryLater(self, now_us);
    }

    sender->state = kISOTP_SENDER_IDLE;
    return kISOTP_EVENT_SENT;
}

static uint8_t sendSegmentedFirstFrame(IsotpLink *self, uint64_t now_us)
{
    IsotpSender *sender = &self->sender;
    uint8_t data[FRAME_PAYLOAD_MAX_FD];
    uint8_t data_size = firstFrameCapacity(self);

    data[0] = (uint8_t)((PCI_FIRST_FRAME << PCI_TYPE_SHIFT) | (sender->length >> FIRST_FRAME_LENGTH_HIGH_SHIFT));
    data[FIRST_FRAME_LENGTH_LOW_OFFSET] = (uint8_t)(sender->length & FIRST_FRAME_LENGTH_LOW_MASK);
    memcpy(data + FIRST_FRAME_HEADER_SIZE, sender->message, data_size);
    if (!transmitData(self, data, (uint8_t)(FIRST_FRAME_HEADER_SIZE + data_size))) {
        return retryLater(self, now_us);
    }

    sender->offset = data_size;
    sender->sequence = FIRST_SEQUENCE;
    sender->state = kISOTP_SENDER_FLOW_CONTROL;
    sender->deadline_us = now_us + ISOTP_TIMEOUT_US;

    return kISOTP_EVENT_NONE;
}

static uint8_t sendConsecutiveFrame(IsotpLink *self, uint64_t now_us)
{
    IsotpSender *sender = &self->sender;
    uint16_t remaining = (uint16_t)(sender->length - sender->offset);
    uint8_t capacity = consecutiveFrameCapacity(self);
    uint8_t size = remaining < capacity ? (uint8_t)remaining : capacity;
    uint8_t data[FRAME_PAYLOAD_MAX_FD];

    data[0] = (uint8_t)((PCI_CONSECUTIVE_FRAME << PCI_TYPE_SHIFT) | sender->sequence);
    memcpy(data + CONSECUTIVE_FRAME_HEADER_SIZE, sender->message + sender->offset, size);
    if (!transmitData(self, data, (uint8_t)(CONSECUTIVE_FRAME_HEADER_SIZE + size))) {
        return retryLater(self, now_us);
    }

    sender->offset = (uint16_t)(sender->offset + size);
    sender->sequence = (uint8_t)((sender->sequence + 1) & SEQUENCE_MASK);
    sender->next_frame_us = now_us + sender->st_min_us;
    sender->deadline_us = now_us + ISOTP_TIMEOUT_US;
    if (sender->offset == sender->length) {
        sender->state = kISOTP_SENDER_IDLE;
        return kISOTP_EVENT_SENT;
    }
    if (!sender->block_limited) {
        return kISOTP_EVENT_NONE;
    }

    sender->block_remaining--;
    if (sender->block_remaining == 0) {
        sender->state = kISOTP_SENDER_FLOW_CONTROL;
    }
    return kISOTP_EVENT_NONE;
}

static uint8_t retryLater(IsotpLink *self, uint64_t now_us)
{
    if (now_us >= self->sender.deadline_us) {
        return fail(self, kTXN_REASON_TX_FAILED);
    }
    self->sender.next_frame_us = now_us + ISOTP_RETRY_US;
    return kISOTP_EVENT_NONE;
}

static bool transmitFlowControl(IsotpLink *self, uint8_t flow_status)
{
    uint8_t data[FLOW_CONTROL_SIZE];

    data[0] = (uint8_t)((PCI_FLOW_CONTROL << PCI_TYPE_SHIFT) | flow_status);
    data[FLOW_CONTROL_BLOCK_SIZE_OFFSET] = self->parameters.block_size;
    data[FLOW_CONTROL_ST_MIN_OFFSET] = self->parameters.st_min;
    return transmitData(self, data, FLOW_CONTROL_SIZE);
}

static bool transmitData(IsotpLink *self, const uint8_t *data, uint8_t size)
{
    FrameMessage frame;
    uint8_t address_size = addressSize(self);
    uint8_t length = frameLength(self, (uint8_t)(address_size + size));

    memset(&frame, 0, sizeof(frame));
    frame.can_id = self->request_id;
    frame.channel = self->channel;
    frame.payload_length = length;
    frame.payload[0] = self->parameters.tx_address;
    memcpy(frame.payload + address_size, data, size);
    memset(frame.payload + address_size + size, self->parameters.padding, (size_t)(length - address_size - size));
    if (self->parameters.flags & TXN_ISOTP_FLAG_FD) {
        frame.frame_flags = FRAME_FLAG_FD;
    }
    if (self->parameters.flags & TXN_ISOTP_FLAG_BIT_RATE_SWITCH) {
        frame.frame_flags |= FRAME_FLAG_BRS;
    }
    return self->port.transmit(self->port.context, &frame);
}

static uint8_t frameLength(const IsotpLink *self, uint8_t size)
{
    uint8_t i;

    if (size <= CLASSIC_FRAME_SIZE) {
        return (self->parameters.flags & TXN_ISOTP_FLAG_PADDING) ? CLASSIC_FRAME_SIZE : size;
    }
    for(i=0; i<sizeof(FD_FRAME_LENGTHS); i++) {
        if (FD_FRAME_LENGTHS[i] >= size) {
            return FD_FRAME_LENGTHS[i];
        }
    }
    return FRAME_PAYLOAD_MAX_FD;
}

static bool isFromPeer(const IsotpLink *self, const FrameMessage *frame)
{
    if (frame->can_id != self->parameters.response_id || frame->payload_length <= addressSize(self)) {
        return false;
    }
    if (addressSize(self) > 0 && frame->payload[0] != self->parameters.rx_address) {
        return false;
    }
    return (frame->route_flags & FRAME_ROUTE_FLAG_ECHO) == 0;
}

static uint8_t addressSize(const IsotpLink *self)
{
    return (self->parameters.flags & TXN_ISOTP_FLAG_ADDRESS_BYTE) ? ADDRESS_BYTE_SIZE : 0;
}

static uint8_t txDataLength(const IsotpLink *self)
{
    return (self->parameters.flags & TXN_ISOTP_FLAG_FD) ? self->parameters.tx_data_length : CLASSIC_FRAME_SIZE;
}

static uint8_t classicSingleFrameCapacity(const IsotpLink *self)
{
    return (uint8_t)(CLASSIC_FRAME_SIZE - addressSize(self) - SINGLE_FRAME_HEADER_SIZE);
}

static uint8_t escapeSingleFrameCapacity(const IsotpLink *self)
{
    if (txDataLength(self) <= CLASSIC_FRAME_SIZE) {
        return 0;
    }
    return (uint8_t)(txDataLength(self) - addressSize(self) - ESCAPE_SINGLE_FRAME_HEADER_SIZE);
}

static uint8_t firstFrameCapacity(const IsotpLink *self)
{
    return (uint8_t)(txDataLength(self) - addressSize(self) - FIRST_FRAME_HEADER_SIZE);
}

static uint8_t consecutiveFrameCapacity(const IsotpLink *self)
{
    return (uint8_t)(txDataLength(self) - addressSize(self) - CONSECUTIVE_FRAME_HEADER_SIZE);
}

static uint32_t stMinUs(uint8_t st_min)
{
    if (st_min <= ST_MIN_MILLISECONDS_MAX) {
        return (uint32_t)st_min * MICROSECONDS_PER_MILLISECOND;
    }
    if (st_min >= ST_MIN_MICROSECONDS_FIRST && st_min <= ST_MIN_MICROSECONDS_LAST) {
        return (uint32_t)(st_min - ST_MIN_MICROSECONDS_BASE) * ST_MIN_MICROSECONDS_STEP;
    }
    return (uint32_t)ST_MIN_MILLISECONDS_MAX * MICROSECONDS_PER_MILLISECOND;
}

static uint8_t fail(IsotpLink *self, uint8_t reason)
{
    self->failure_reason = reason;
    self->sender.state = kISOTP_SENDER_IDLE;
    self->receiver.state = kISOTP_RECEIVER_IDLE;
    self->receiver.flow_control_pending = false;
    return kISOTP_EVENT_FAILED;
}

static uint64_t earliest(uint64_t current_us, uint64_t candidate_us)
{
    if (candidate_us == 0) {
        return current_us;
    }
    if (current_us == 0 || candidate_us < current_us) {
        return candidate_us;
    }
    return current_us;
}
