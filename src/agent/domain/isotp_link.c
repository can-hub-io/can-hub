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
#define SINGLE_FRAME_DATA_MAX 7
#define FIRST_FRAME_HEADER_SIZE 2
#define FIRST_FRAME_DATA_SIZE 6
#define FIRST_FRAME_LENGTH_MIN 8
#define FIRST_FRAME_LENGTH_HIGH_SHIFT 8
#define FIRST_FRAME_LENGTH_LOW_MASK 0xFF
#define CONSECUTIVE_FRAME_HEADER_SIZE 1
#define CONSECUTIVE_FRAME_DATA_MAX 7
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

static uint8_t onFlowControl(IsotpLink *self, const FrameMessage *frame, uint64_t now_us);
static uint8_t onSingleFrame(IsotpLink *self, const FrameMessage *frame);
static uint8_t onFirstFrame(IsotpLink *self, const FrameMessage *frame, uint64_t now_us);
static uint8_t onConsecutiveFrame(IsotpLink *self, const FrameMessage *frame, uint64_t now_us);
static uint8_t tickReceiver(IsotpLink *self, uint64_t now_us);
static uint8_t tickSender(IsotpLink *self, uint64_t now_us);
static uint8_t sendFirstFrame(IsotpLink *self, uint64_t now_us);
static uint8_t sendConsecutiveFrame(IsotpLink *self, uint64_t now_us);
static uint8_t retryLater(IsotpLink *self, uint64_t now_us);
static bool transmitFlowControl(IsotpLink *self, uint8_t flow_status);
static bool transmitData(IsotpLink *self, const uint8_t *data, uint8_t size);
static bool isFromPeer(const IsotpLink *self, const FrameMessage *frame);
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

    pci_type = frame->payload[0] >> PCI_TYPE_SHIFT;
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
    uint8_t flow_status = frame->payload[0] & PCI_LOW_MASK;

    if (sender->state != kISOTP_SENDER_FLOW_CONTROL || frame->payload_length < FLOW_CONTROL_SIZE) {
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
    sender->block_remaining = frame->payload[FLOW_CONTROL_BLOCK_SIZE_OFFSET];
    sender->block_limited = sender->block_remaining != 0;
    sender->st_min_us = stMinUs(frame->payload[FLOW_CONTROL_ST_MIN_OFFSET]);
    sender->state = kISOTP_SENDER_CONSECUTIVE_FRAMES;
    sender->next_frame_us = now_us;
    sender->deadline_us = now_us + ISOTP_TIMEOUT_US;

    return kISOTP_EVENT_NONE;
}

static uint8_t onSingleFrame(IsotpLink *self, const FrameMessage *frame)
{
    IsotpReceiver *receiver = &self->receiver;
    uint8_t length = frame->payload[0] & PCI_LOW_MASK;

    if (length == 0 || length > SINGLE_FRAME_DATA_MAX || frame->payload_length < SINGLE_FRAME_HEADER_SIZE + length) {
        return kISOTP_EVENT_NONE;
    }

    memcpy(receiver->message, frame->payload + SINGLE_FRAME_HEADER_SIZE, length);
    receiver->length = length;
    receiver->state = kISOTP_RECEIVER_IDLE;
    receiver->flow_control_pending = false;

    return kISOTP_EVENT_MESSAGE;
}

static uint8_t onFirstFrame(IsotpLink *self, const FrameMessage *frame, uint64_t now_us)
{
    IsotpReceiver *receiver = &self->receiver;
    uint16_t high_bits;
    uint16_t length;

    if (frame->payload_length < CLASSIC_FRAME_SIZE) {
        return kISOTP_EVENT_NONE;
    }
    high_bits = (uint16_t)(frame->payload[0] & PCI_LOW_MASK) << FIRST_FRAME_LENGTH_HIGH_SHIFT;
    length = (uint16_t)(high_bits | frame->payload[FIRST_FRAME_LENGTH_LOW_OFFSET]);
    if (length == 0) {
        transmitFlowControl(self, FLOW_STATUS_OVERFLOW);
        return fail(self, kTXN_REASON_OVERFLOW);
    }
    if (length < FIRST_FRAME_LENGTH_MIN) {
        return kISOTP_EVENT_NONE;
    }

    memcpy(receiver->message, frame->payload + FIRST_FRAME_HEADER_SIZE, FIRST_FRAME_DATA_SIZE);
    receiver->length = length;
    receiver->received = FIRST_FRAME_DATA_SIZE;
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
    uint16_t remaining = (uint16_t)(receiver->length - receiver->received);
    uint8_t size = remaining < CONSECUTIVE_FRAME_DATA_MAX ? (uint8_t)remaining : CONSECUTIVE_FRAME_DATA_MAX;

    if (receiver->state != kISOTP_RECEIVER_CONSECUTIVE_FRAMES || receiver->flow_control_pending) {
        return kISOTP_EVENT_NONE;
    }
    if ((frame->payload[0] & PCI_LOW_MASK) != receiver->sequence) {
        return fail(self, kTXN_REASON_SEQUENCE_ERROR);
    }
    if (frame->payload_length < CONSECUTIVE_FRAME_HEADER_SIZE + size) {
        return kISOTP_EVENT_NONE;
    }

    memcpy(receiver->message + receiver->received, frame->payload + CONSECUTIVE_FRAME_HEADER_SIZE, size);
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
    IsotpSender *sender = &self->sender;
    uint8_t data[CLASSIC_FRAME_SIZE];

    if (sender->length <= SINGLE_FRAME_DATA_MAX) {
        data[0] = (uint8_t)sender->length;
        memcpy(data + SINGLE_FRAME_HEADER_SIZE, sender->message, sender->length);
        if (!transmitData(self, data, (uint8_t)(SINGLE_FRAME_HEADER_SIZE + sender->length))) {
            return retryLater(self, now_us);
        }
        sender->state = kISOTP_SENDER_IDLE;
        return kISOTP_EVENT_SENT;
    }

    data[0] = (uint8_t)((PCI_FIRST_FRAME << PCI_TYPE_SHIFT) | (sender->length >> FIRST_FRAME_LENGTH_HIGH_SHIFT));
    data[FIRST_FRAME_LENGTH_LOW_OFFSET] = (uint8_t)(sender->length & FIRST_FRAME_LENGTH_LOW_MASK);
    memcpy(data + FIRST_FRAME_HEADER_SIZE, sender->message, FIRST_FRAME_DATA_SIZE);
    if (!transmitData(self, data, CLASSIC_FRAME_SIZE)) {
        return retryLater(self, now_us);
    }

    sender->offset = FIRST_FRAME_DATA_SIZE;
    sender->sequence = FIRST_SEQUENCE;
    sender->state = kISOTP_SENDER_FLOW_CONTROL;
    sender->deadline_us = now_us + ISOTP_TIMEOUT_US;

    return kISOTP_EVENT_NONE;
}

static uint8_t sendConsecutiveFrame(IsotpLink *self, uint64_t now_us)
{
    IsotpSender *sender = &self->sender;
    uint16_t remaining = (uint16_t)(sender->length - sender->offset);
    uint8_t size = remaining < CONSECUTIVE_FRAME_DATA_MAX ? (uint8_t)remaining : CONSECUTIVE_FRAME_DATA_MAX;
    uint8_t data[CLASSIC_FRAME_SIZE];

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

    memset(&frame, 0, sizeof(frame));
    frame.can_id = self->request_id;
    frame.channel = self->channel;
    frame.payload_length = size;
    memcpy(frame.payload, data, size);
    if (self->parameters.flags & TXN_ISOTP_FLAG_PADDING) {
        memset(frame.payload + size, self->parameters.padding, CLASSIC_FRAME_SIZE - size);
        frame.payload_length = CLASSIC_FRAME_SIZE;
    }
    return self->port.transmit(self->port.context, &frame);
}

static bool isFromPeer(const IsotpLink *self, const FrameMessage *frame)
{
    if (frame->can_id != self->parameters.response_id || frame->payload_length == 0) {
        return false;
    }
    return (frame->route_flags & FRAME_ROUTE_FLAG_ECHO) == 0;
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
