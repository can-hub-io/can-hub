#include "agent/domain/transaction_runner.h"

#include <string.h>

#define MICROSECONDS_PER_MILLISECOND 1000
#define PROGRESS_THRESHOLD_DIVISOR 4
#define ORIGIN_PAYLOAD_BYTES 4
#define BITS_PER_BYTE 8
#define ORIGIN_SHIFT_MAX 32
#define NO_TXN_ID 0
#define ISOTP_FLAGS_KNOWN (TXN_ISOTP_FLAG_PADDING | TXN_ISOTP_FLAG_ADDRESS_BYTE | TXN_ISOTP_FLAG_FD | TXN_ISOTP_FLAG_BIT_RATE_SWITCH)

static bool isBeginValid(const TxnBeginMessage *begin);
static bool isRosterValid(const TxnRosterMessage *roster);
static bool isRosterMode(uint8_t advance_mode);
static void reportMissingRoster(TransactionRunner *self);
static bool isTemplateValid(const TransactionRunner *self, const TxnTemplateMessage *template_message);
static bool isTypeValid(const TxnTemplateMessage *template_message);
static bool isIsotpDataLengthValid(const TxnIsotpParameters *isotp);
static bool fitsTemplate(const TxnStep *step, const TxnTemplateMessage *template_message);
static bool isWaitingMode(uint8_t advance_mode);
static bool isTemplateDefined(const TransactionRunner *self, uint8_t template_id);
static bool isPatternPresent(const TxnTemplateMessage *template_message, uint8_t pattern);
static bool pushRecord(TransactionRunner *self, const TxnStep *step, const uint8_t *record, size_t record_size, uint32_t seq);
static bool runOnce(TransactionRunner *self, uint64_t now_us);
static bool loadNextStep(TransactionRunner *self, uint64_t now_us);
static bool transmitStep(TransactionRunner *self, uint64_t now_us);
static bool transmitMessage(TransactionRunner *self, uint64_t now_us);
static bool finishTransmit(TransactionRunner *self, uint64_t now_us);
static bool gatherMessage(TransactionRunner *self);
static bool tickIsotp(TransactionRunner *self, uint64_t now_us);
static void feedIsotp(TransactionRunner *self, const FrameMessage *frame);
static bool isIsotpStep(const TransactionRunner *self);
static bool transmitIsotpFrame(void *context, const FrameMessage *frame);
static void releaseRecord(TransactionRunner *self);
static uint64_t idleTimeoutUs(const TxnBeginMessage *begin);
static bool waitForSteps(TransactionRunner *self, uint64_t now_us);
static void sendKeepaliveIfDue(TransactionRunner *self, uint64_t now_us);
static uint64_t keepaliveDueUs(const TransactionRunner *self);
static bool transmitTracked(TransactionRunner *self, const FrameMessage *frame, uint64_t now_us);
static bool settleWait(TransactionRunner *self, uint64_t now_us);
static void startWait(TransactionRunner *self, uint64_t now_us);
static void completeStep(TransactionRunner *self);
static void finishDone(TransactionRunner *self);
static void flushProgress(TransactionRunner *self, uint64_t now_us);
static void reportProgress(TransactionRunner *self);
static bool isOnBus(const TransactionRunner *self, uint8_t interface_index, const FrameMessage *frame);
static bool isWaiting(const TransactionRunner *self);
static bool isBetweenSteps(const TransactionRunner *self);
static void keepEarlyFrame(TransactionRunner *self, const FrameMessage *frame);
static void replayEarlyFrames(TransactionRunner *self);
static void dispatchFrame(TransactionRunner *self, const FrameMessage *frame);
static void matchFrame(TransactionRunner *self, const FrameMessage *frame);
static void recordAnswer(TransactionRunner *self, const FrameMessage *frame);
static void recordRosterAnswer(TransactionRunner *self, const FrameMessage *frame);
static bool findRosterIndex(const TransactionRunner *self, uint32_t origin, uint8_t *roster_index);
static bool matchesId(const TxnPattern *pattern, uint32_t can_id);
static bool matchesPayload(uint64_t payload_word, uint64_t payload_mask, uint64_t payload_value);
static bool matchesPattern(const TxnPattern *pattern, const FrameMessage *frame, uint64_t payload_word);
static bool matchesTemplatePattern(
    const TxnTemplateMessage *template_message,
    uint8_t pattern,
    const FrameMessage *frame,
    uint64_t payload_word
);
static void matchAdvance(
    TransactionRunner *self,
    const TxnTemplateMessage *template_message,
    const FrameMessage *frame,
    uint64_t payload_word
);
static uint64_t expectedValue(const TransactionRunner *self, const TxnTemplateMessage *template_message);
static uint64_t payloadWord(const FrameMessage *frame);
static uint32_t payloadOrigin(const FrameMessage *frame, uint8_t offset);
static uint32_t extractOrigin(const TransactionRunner *self, const FrameMessage *frame);
static void keepRxFrame(TransactionRunner *self, const FrameMessage *frame);
static const TxnTemplateMessage *currentTemplate(const TransactionRunner *self);
static uint64_t templateTimeoutUs(const TxnTemplateMessage *template_message);
static uint64_t transmitTimeoutUs(const TxnTemplateMessage *template_message);
static uint64_t extendTimeoutUs(const TxnTemplateMessage *template_message);
static void prepareStatus(const TransactionRunner *self, TxnStatusMessage *status, uint8_t state, uint8_t reason);
static void finish(TransactionRunner *self, const TxnStatusMessage *status);
static void abortWith(TransactionRunner *self, uint8_t reason, uint32_t failing_seq);
static void abortOnFrame(TransactionRunner *self, uint8_t reason, const FrameMessage *frame, uint64_t expected, uint64_t got);
static uint64_t earliestWake(uint64_t current, uint64_t candidate);
static uint64_t transmitReadyUs(const TransactionRunner *self);

/* ---------- public ---------- */

void TransactionRunner_Init(TransactionRunner *self, TransactionRunnerPort port, uint8_t *ring_storage, uint32_t ring_capacity)
{
    IsotpLinkPort isotp_port = { self, transmitIsotpFrame };

    memset(self, 0, sizeof(*self));
    self->port = port;
    IsotpLink_Init(&self->isotp, isotp_port);
    StepRing_Init(&self->ring, ring_storage, ring_capacity);
    self->window_bytes = ring_capacity - TXN_STEP_RECORD_SIZE_MAX;
}

void TransactionRunner_Begin(
    TransactionRunner *self,
    const TxnBeginMessage *begin,
    uint8_t interface_index,
    uint64_t now_us,
    TxnBeginAckMessage *ack
)
{
    ack->channel = begin->channel;
    ack->txn_id = NO_TXN_ID;
    ack->window_bytes = 0;

    if (self->active) {
        ack->status = kTXN_BEGIN_STATUS_BUSY;
        return;
    }
    if (!isBeginValid(begin)) {
        ack->status = kTXN_BEGIN_STATUS_INVALID;
        return;
    }

    self->txn_id++;
    if (self->txn_id == NO_TXN_ID) {
        self->txn_id++;
    }
    self->active = true;
    self->channel = begin->channel;
    self->interface_index = interface_index;
    memset(&self->origin, 0, sizeof(self->origin));
    self->roster_count = 0;
    self->defined_templates = 0;
    self->next_seq = 0;
    self->steps_ended = false;
    self->completed_steps = 0;
    self->pushed_bytes = 0;
    self->consumed_bytes = 0;
    self->reported_bytes = 0;
    self->report_due_us = 0;
    self->idle_timeout_us = idleTimeoutUs(begin);
    self->keepalive = begin->keepalive;
    self->last_frame_us = now_us;
    self->starve_deadline_us = now_us + self->idle_timeout_us;
    self->next_transmit_us = 0;
    self->last_tick_us = now_us;
    self->message_length = 0;
    self->early_frame_count = 0;
    memset(&self->current, 0, sizeof(self->current));
    StepRing_Clear(&self->ring);

    ack->status = kTXN_BEGIN_STATUS_OK;
    ack->txn_id = self->txn_id;
    ack->window_bytes = self->window_bytes;
}

void TransactionRunner_DefineRoster(TransactionRunner *self, const TxnRosterMessage *roster)
{
    if (!self->active || roster->txn_id != self->txn_id) {
        return;
    }
    if (self->roster_count != 0 || !isRosterValid(roster)) {
        abortWith(self, kTXN_REASON_INVALID_STEP, self->next_seq);
        return;
    }

    self->origin = roster->origin;
    self->roster_count = roster->roster_count;
    memcpy(self->roster, roster->roster, sizeof(self->roster));
}

void TransactionRunner_DefineTemplate(TransactionRunner *self, const TxnTemplateMessage *template_message)
{
    if (!self->active || template_message->txn_id != self->txn_id) {
        return;
    }
    if (!isTemplateValid(self, template_message)) {
        abortWith(self, kTXN_REASON_INVALID_STEP, self->next_seq);
        return;
    }

    self->templates[template_message->template_id] = *template_message;
    self->defined_templates |= (uint16_t)(1u << template_message->template_id);
}

void TransactionRunner_PushSteps(TransactionRunner *self, const TxnStepsMessage *steps)
{
    TxnStep step;
    const uint8_t *record;
    size_t offset = 0;
    size_t record_size;
    uint32_t seq = steps->first_seq;
    uint8_t i;

    if (!self->active || steps->txn_id != self->txn_id) {
        return;
    }
    if (self->steps_ended || steps->first_seq != self->next_seq) {
        abortWith(self, kTXN_REASON_INVALID_STEP, steps->first_seq);
        return;
    }

    for(i=0; i<steps->step_count; i++) {
        record = steps->records + offset;
        record_size = TxnStep_Decode(&step, record, steps->records_size - offset);
        if (!pushRecord(self, &step, record, record_size, seq)) {
            return;
        }
        offset += record_size;
        if ((step.flags & TXN_STEP_FLAG_CONTINUED) == 0) {
            seq++;
        }
    }
    self->next_seq = seq;
    self->steps_ended = (steps->flags & TXN_STEPS_FLAG_END) != 0;
}

void TransactionRunner_OnFrame(TransactionRunner *self, uint8_t interface_index, const FrameMessage *frame)
{
    if (!isOnBus(self, interface_index, frame)) {
        return;
    }
    if (isBetweenSteps(self)) {
        keepEarlyFrame(self, frame);
        return;
    }
    dispatchFrame(self, frame);
}

void TransactionRunner_Tick(TransactionRunner *self, uint64_t now_us)
{
    uint8_t i;

    if (!self->active) {
        return;
    }

    self->last_tick_us = now_us;
    for(i=0; i<TRANSACTION_STEPS_PER_TICK_MAX && self->active; i++) {
        if (!runOnce(self, now_us)) {
            break;
        }
    }
    if (self->active) {
        flushProgress(self, now_us);
    }
}

void TransactionRunner_Abort(TransactionRunner *self, uint16_t txn_id)
{
    if (!self->active || txn_id != self->txn_id) {
        return;
    }

    abortWith(self, kTXN_REASON_CANCELLED, self->completed_steps);
}

void TransactionRunner_Cancel(TransactionRunner *self)
{
    self->active = false;
    StepRing_Clear(&self->ring);
}

bool TransactionRunner_IsActive(const TransactionRunner *self)
{
    return self->active;
}

uint64_t TransactionRunner_NextWakeUs(const TransactionRunner *self)
{
    uint64_t wake_us = 0;

    if (!self->active) {
        return 0;
    }

    wake_us = earliestWake(wake_us, self->report_due_us);
    if (self->current.phase == kTRANSACTION_PHASE_IDLE) {
        if (!StepRing_IsEmpty(&self->ring) || self->steps_ended) {
            return self->last_tick_us;
        }
        if (self->keepalive.period_ms != 0) {
            wake_us = earliestWake(wake_us, keepaliveDueUs(self));
        }
        return earliestWake(wake_us, self->starve_deadline_us);
    }
    if (self->current.phase == kTRANSACTION_PHASE_TRANSMIT && self->current.message_started) {
        return earliestWake(wake_us, IsotpLink_NextWakeUs(&self->isotp));
    }
    if (self->current.phase == kTRANSACTION_PHASE_TRANSMIT) {
        return earliestWake(wake_us, transmitReadyUs(self));
    }
    wake_us = earliestWake(wake_us, self->current.deadline_us);
    if (isIsotpStep(self)) {
        wake_us = earliestWake(wake_us, IsotpLink_NextWakeUs(&self->isotp));
    }
    return wake_us;
}

/* ---------- private ---------- */

static bool isBeginValid(const TxnBeginMessage *begin)
{
    const TxnKeepalive *keepalive = &begin->keepalive;

    return keepalive->period_ms == 0 || FrameMessage_IsPayloadLengthValid(keepalive->payload_length, keepalive->frame_flags);
}

static bool isRosterValid(const TxnRosterMessage *roster)
{
    if (roster->roster_count == 0 || roster->roster_count > TXN_ROSTER_MAX) {
        return false;
    }
    if (roster->origin.source != kTXN_ORIGIN_SOURCE_CAN_ID && roster->origin.source != kTXN_ORIGIN_SOURCE_PAYLOAD) {
        return false;
    }
    if (roster->origin.shift >= ORIGIN_SHIFT_MAX) {
        return false;
    }
    return roster->origin.payload_offset < FRAME_PAYLOAD_MAX_FD;
}

static bool isRosterMode(uint8_t advance_mode)
{
    return advance_mode == kTXN_ADVANCE_MODE_ROSTER_ALL || advance_mode == kTXN_ADVANCE_MODE_ROSTER_K;
}

static void reportMissingRoster(TransactionRunner *self)
{
    TxnRosterReportMessage report;

    if (!isRosterMode(currentTemplate(self)->advance_mode) || self->current.seen_count >= self->roster_count) {
        return;
    }

    report.channel = self->channel;
    report.txn_id = self->txn_id;
    report.seq = self->completed_steps;
    report.seen_bitmap = self->current.seen_bitmap;
    self->port.report_roster(self->port.context, &report);
}

static bool isTemplateValid(const TransactionRunner *self, const TxnTemplateMessage *template_message)
{
    uint8_t mode = template_message->advance_mode;

    if (isTemplateDefined(self, template_message->template_id)) {
        return false;
    }
    if (mode >= kTXN_ADVANCE_MODE_MAX || !isTypeValid(template_message)) {
        return false;
    }
    if (isWaitingMode(mode) && template_message->timeout_ms == 0) {
        return false;
    }
    if ((mode == kTXN_ADVANCE_MODE_COUNT || mode == kTXN_ADVANCE_MODE_ROSTER_K) && template_message->count_k == 0) {
        return false;
    }
    if (isRosterMode(mode) && self->roster_count == 0) {
        return false;
    }
    return mode != kTXN_ADVANCE_MODE_ROSTER_K || template_message->count_k <= self->roster_count;
}

static bool isTypeValid(const TxnTemplateMessage *template_message)
{
    const TxnIsotpParameters *isotp = &template_message->isotp;
    uint8_t mode = template_message->advance_mode;

    if (template_message->type == kTXN_TEMPLATE_TYPE_GENERIC) {
        return true;
    }
    if (template_message->type != kTXN_TEMPLATE_TYPE_ISOTP) {
        return false;
    }
    if ((isotp->flags & ~ISOTP_FLAGS_KNOWN) != 0 || !isIsotpDataLengthValid(isotp)) {
        return false;
    }
    return mode == kTXN_ADVANCE_MODE_NONE || mode == kTXN_ADVANCE_MODE_COUNT;
}

static bool isIsotpDataLengthValid(const TxnIsotpParameters *isotp)
{
    if ((isotp->flags & TXN_ISOTP_FLAG_FD) == 0) {
        return isotp->tx_data_length == FRAME_PAYLOAD_MAX_CLASSIC && (isotp->flags & TXN_ISOTP_FLAG_BIT_RATE_SWITCH) == 0;
    }
    return isotp->tx_data_length >= FRAME_PAYLOAD_MAX_CLASSIC && FrameMessage_IsPayloadLengthValid(isotp->tx_data_length, FRAME_FLAG_FD);
}

static bool fitsTemplate(const TxnStep *step, const TxnTemplateMessage *template_message)
{
    bool message = (step->flags & TXN_STEP_FLAG_MESSAGE) != 0;

    if (template_message->type == kTXN_TEMPLATE_TYPE_ISOTP) {
        return message || (step->flags & TXN_STEP_FLAG_EXPECT_ONLY) != 0;
    }
    return !message;
}

static bool isWaitingMode(uint8_t advance_mode)
{
    return advance_mode != kTXN_ADVANCE_MODE_NONE;
}

static bool isTemplateDefined(const TransactionRunner *self, uint8_t template_id)
{
    return (self->defined_templates & (1u << template_id)) != 0;
}

static bool isPatternPresent(const TxnTemplateMessage *template_message, uint8_t pattern)
{
    return (template_message->patterns_present & (1u << pattern)) != 0;
}

static bool pushRecord(TransactionRunner *self, const TxnStep *step, const uint8_t *record, size_t record_size, uint32_t seq)
{
    const TxnTemplateMessage *template_message;
    uint32_t in_flight;

    if (record_size == 0 || !isTemplateDefined(self, step->template_id)) {
        abortWith(self, kTXN_REASON_INVALID_STEP, seq);
        return false;
    }
    template_message = &self->templates[step->template_id];
    if (!fitsTemplate(step, template_message)) {
        abortWith(self, kTXN_REASON_INVALID_STEP, seq);
        return false;
    }
    if ((step->flags & TXN_STEP_FLAG_EXPECT_ONLY) && !isWaitingMode(template_message->advance_mode)) {
        abortWith(self, kTXN_REASON_INVALID_STEP, seq);
        return false;
    }

    in_flight = self->pushed_bytes - self->consumed_bytes;
    if (in_flight + record_size > self->window_bytes || !StepRing_Push(&self->ring, record, (uint32_t)record_size)) {
        abortWith(self, kTXN_REASON_OVERRUN, seq);
        return false;
    }

    self->pushed_bytes += (uint32_t)record_size;

    return true;
}

static bool runOnce(TransactionRunner *self, uint64_t now_us)
{
    if (self->current.phase == kTRANSACTION_PHASE_IDLE) {
        return loadNextStep(self, now_us);
    }
    if (self->current.phase == kTRANSACTION_PHASE_TRANSMIT) {
        return transmitStep(self, now_us);
    }
    return settleWait(self, now_us);
}

static bool loadNextStep(TransactionRunner *self, uint64_t now_us)
{
    const uint8_t *record;
    uint32_t available;

    available = StepRing_Peek(&self->ring, &record);
    if (available == 0 && self->steps_ended) {
        finishDone(self);
        return false;
    }
    if (available == 0) {
        return waitForSteps(self, now_us);
    }

    memset(&self->current, 0, sizeof(self->current));
    self->current.record_size = (uint32_t)TxnStep_Decode(&self->current.step, record, available);
    self->starve_deadline_us = 0;

    if ((self->current.step.flags & TXN_STEP_FLAG_MESSAGE) && !gatherMessage(self)) {
        return self->active;
    }
    if (currentTemplate(self)->type == kTXN_TEMPLATE_TYPE_ISOTP) {
        IsotpLink_Open(&self->isotp, self->current.step.can_id, self->channel, &currentTemplate(self)->isotp);
    }
    if (self->current.step.flags & TXN_STEP_FLAG_EXPECT_ONLY) {
        startWait(self, now_us);
        replayEarlyFrames(self);
        return self->active;
    }

    self->current.phase = kTRANSACTION_PHASE_TRANSMIT;
    self->early_frame_count = 0;

    return true;
}

static bool transmitStep(TransactionRunner *self, uint64_t now_us)
{
    const TxnTemplateMessage *template_message = currentTemplate(self);
    FrameMessage frame;

    if (now_us < self->next_transmit_us || now_us < self->current.retry_at_us) {
        return false;
    }
    if (isIsotpStep(self)) {
        return transmitMessage(self, now_us);
    }
    if (self->current.deadline_us == 0) {
        self->current.deadline_us = now_us + transmitTimeoutUs(template_message);
    }

    memset(&frame, 0, sizeof(frame));
    frame.can_id = self->current.step.can_id;
    frame.channel = self->channel;
    frame.payload_length = self->current.step.payload_length;
    frame.frame_flags = self->current.step.frame_flags;
    memcpy(frame.payload, self->current.step.payload, frame.payload_length);

    if (!transmitTracked(self, &frame, now_us)) {
        if (now_us >= self->current.deadline_us) {
            abortWith(self, kTXN_REASON_TX_FAILED, self->completed_steps);
            return false;
        }
        self->current.retry_at_us = now_us + TRANSACTION_TX_RETRY_US;
        return false;
    }

    return finishTransmit(self, now_us);
}

static bool transmitMessage(TransactionRunner *self, uint64_t now_us)
{
    uint8_t event;

    if (!self->current.message_started) {
        IsotpLink_Send(&self->isotp, self->message, self->message_length, now_us);
        self->current.message_started = true;
    }

    event = IsotpLink_Tick(&self->isotp, now_us);
    if (event == kISOTP_EVENT_FAILED) {
        abortWith(self, IsotpLink_FailureReason(&self->isotp), self->completed_steps);
        return false;
    }
    if (event != kISOTP_EVENT_SENT) {
        return false;
    }
    return finishTransmit(self, now_us);
}

static bool finishTransmit(TransactionRunner *self, uint64_t now_us)
{
    const TxnTemplateMessage *template_message = currentTemplate(self);

    self->next_transmit_us = now_us + template_message->gap_us;
    if (!isWaitingMode(template_message->advance_mode)) {
        completeStep(self);
        return true;
    }

    startWait(self, now_us);

    return true;
}

static bool gatherMessage(TransactionRunner *self)
{
    const TxnStep *step = &self->current.step;

    if (self->message_length + step->payload_length > TXN_ISOTP_MESSAGE_SIZE_MAX) {
        abortWith(self, kTXN_REASON_INVALID_STEP, self->completed_steps);
        return false;
    }

    memcpy(self->message + self->message_length, step->payload, step->payload_length);
    self->message_length = (uint16_t)(self->message_length + step->payload_length);
    if ((step->flags & TXN_STEP_FLAG_CONTINUED) == 0) {
        return true;
    }

    releaseRecord(self);
    return false;
}

static bool tickIsotp(TransactionRunner *self, uint64_t now_us)
{
    if (IsotpLink_Tick(&self->isotp, now_us) != kISOTP_EVENT_FAILED) {
        return true;
    }
    abortWith(self, IsotpLink_FailureReason(&self->isotp), self->completed_steps);
    return false;
}

static void feedIsotp(TransactionRunner *self, const FrameMessage *frame)
{
    FrameMessage message_frame;
    uint8_t event = IsotpLink_OnFrame(&self->isotp, frame, self->last_tick_us);

    if (event == kISOTP_EVENT_FAILED) {
        abortWith(self, IsotpLink_FailureReason(&self->isotp), self->completed_steps);
        return;
    }
    if (event != kISOTP_EVENT_MESSAGE || !isWaiting(self)) {
        return;
    }

    IsotpLink_MessageFrame(&self->isotp, &message_frame);
    matchFrame(self, &message_frame);
}

static bool isIsotpStep(const TransactionRunner *self)
{
    if (self->current.phase == kTRANSACTION_PHASE_IDLE) {
        return false;
    }
    return currentTemplate(self)->type == kTXN_TEMPLATE_TYPE_ISOTP;
}

static bool transmitIsotpFrame(void *context, const FrameMessage *frame)
{
    TransactionRunner *self = context;

    return transmitTracked(self, frame, self->last_tick_us);
}

static uint64_t idleTimeoutUs(const TxnBeginMessage *begin)
{
    if (begin->idle_timeout_ms == 0) {
        return TRANSACTION_STARVATION_TIMEOUT_US;
    }
    return (uint64_t)begin->idle_timeout_ms * MICROSECONDS_PER_MILLISECOND;
}

static bool waitForSteps(TransactionRunner *self, uint64_t now_us)
{
    if (self->starve_deadline_us == 0) {
        self->starve_deadline_us = now_us + self->idle_timeout_us;
    }
    if (now_us >= self->starve_deadline_us) {
        abortWith(self, kTXN_REASON_STARVED, self->completed_steps);
        return false;
    }
    sendKeepaliveIfDue(self, now_us);
    return false;
}

static void sendKeepaliveIfDue(TransactionRunner *self, uint64_t now_us)
{
    FrameMessage frame;

    if (self->keepalive.period_ms == 0 || now_us < keepaliveDueUs(self)) {
        return;
    }

    memset(&frame, 0, sizeof(frame));
    frame.can_id = self->keepalive.can_id;
    frame.channel = self->channel;
    frame.payload_length = self->keepalive.payload_length;
    frame.frame_flags = self->keepalive.frame_flags;
    memcpy(frame.payload, self->keepalive.payload, frame.payload_length);
    if (transmitTracked(self, &frame, now_us)) {
        self->early_frame_count = 0;
    }
}

static uint64_t keepaliveDueUs(const TransactionRunner *self)
{
    return self->last_frame_us + (uint64_t)self->keepalive.period_ms * MICROSECONDS_PER_MILLISECOND;
}

static bool transmitTracked(TransactionRunner *self, const FrameMessage *frame, uint64_t now_us)
{
    if (!self->port.transmit(self->port.context, self->interface_index, frame)) {
        return false;
    }
    self->last_frame_us = now_us;
    return true;
}

static void releaseRecord(TransactionRunner *self)
{
    StepRing_Release(&self->ring, self->current.record_size);
    self->consumed_bytes += self->current.record_size;
}

static bool settleWait(TransactionRunner *self, uint64_t now_us)
{
    const TxnTemplateMessage *template_message = currentTemplate(self);

    if (isIsotpStep(self) && !tickIsotp(self, now_us)) {
        return false;
    }
    if (self->current.satisfied) {
        completeStep(self);
        return true;
    }
    if (self->current.extend_requested) {
        self->current.extend_requested = false;
        self->current.deadline_us = now_us + extendTimeoutUs(template_message);
    }
    if (now_us < self->current.deadline_us) {
        return false;
    }
    if (template_message->flags & TXN_TEMPLATE_FLAG_SOFT) {
        completeStep(self);
        return true;
    }

    abortWith(self, kTXN_REASON_TIMEOUT, self->completed_steps);

    return false;
}

static void startWait(TransactionRunner *self, uint64_t now_us)
{
    self->current.phase = kTRANSACTION_PHASE_WAIT;
    self->current.deadline_us = now_us + templateTimeoutUs(currentTemplate(self));
}

static void completeStep(TransactionRunner *self)
{
    reportMissingRoster(self);
    releaseRecord(self);
    self->message_length = 0;
    self->completed_steps++;
    self->current.phase = kTRANSACTION_PHASE_IDLE;
}

static void finishDone(TransactionRunner *self)
{
    TxnStatusMessage status;

    prepareStatus(self, &status, kTXN_STATE_DONE, kTXN_REASON_NONE);
    finish(self, &status);
}

static void flushProgress(TransactionRunner *self, uint64_t now_us)
{
    uint32_t pending = self->consumed_bytes - self->reported_bytes;

    if (pending == 0) {
        return;
    }
    if (pending >= self->window_bytes / PROGRESS_THRESHOLD_DIVISOR) {
        reportProgress(self);
        return;
    }
    if (self->report_due_us == 0) {
        self->report_due_us = now_us + TRANSACTION_PROGRESS_FLUSH_US;
        return;
    }
    if (now_us >= self->report_due_us) {
        reportProgress(self);
    }
}

static void reportProgress(TransactionRunner *self)
{
    TxnProgressMessage progress;

    progress.channel = self->channel;
    progress.txn_id = self->txn_id;
    progress.completed_steps = self->completed_steps;
    progress.consumed_bytes = self->consumed_bytes;
    self->reported_bytes = self->consumed_bytes;
    self->report_due_us = 0;

    self->port.report_progress(self->port.context, &progress);
}

static bool isOnBus(const TransactionRunner *self, uint8_t interface_index, const FrameMessage *frame)
{
    if (!self->active || interface_index != self->interface_index) {
        return false;
    }
    return (frame->route_flags & FRAME_ROUTE_FLAG_ECHO) == 0;
}

static bool isWaiting(const TransactionRunner *self)
{
    return self->current.phase == kTRANSACTION_PHASE_WAIT && !self->current.satisfied;
}

static bool isBetweenSteps(const TransactionRunner *self)
{
    if (self->current.phase == kTRANSACTION_PHASE_IDLE) {
        return true;
    }
    return self->current.phase == kTRANSACTION_PHASE_WAIT && self->current.satisfied;
}

static void keepEarlyFrame(TransactionRunner *self, const FrameMessage *frame)
{
    if (self->early_frame_count == TRANSACTION_EARLY_FRAMES_MAX) {
        return;
    }
    self->early_frames[self->early_frame_count++] = *frame;
}

static void replayEarlyFrames(TransactionRunner *self)
{
    uint8_t count = self->early_frame_count;
    uint8_t i;

    self->early_frame_count = 0;
    for(i=0; i<count && self->active; i++) {
        dispatchFrame(self, &self->early_frames[i]);
    }
}

static void dispatchFrame(TransactionRunner *self, const FrameMessage *frame)
{
    if (isIsotpStep(self)) {
        feedIsotp(self, frame);
        return;
    }
    if (isWaiting(self)) {
        matchFrame(self, frame);
    }
}

static void matchFrame(TransactionRunner *self, const FrameMessage *frame)
{
    const TxnTemplateMessage *template_message = currentTemplate(self);
    uint64_t payload_word = payloadWord(frame);

    if (matchesTemplatePattern(template_message, kTXN_PATTERN_EXTEND, frame, payload_word)) {
        keepRxFrame(self, frame);
        self->current.extend_requested = true;
        return;
    }
    if (matchesTemplatePattern(template_message, kTXN_PATTERN_ABORT, frame, payload_word)) {
        abortOnFrame(self, kTXN_REASON_ABORT_MATCH, frame, 0, payload_word);
        return;
    }
    if (isPatternPresent(template_message, kTXN_PATTERN_ADVANCE)) {
        matchAdvance(self, template_message, frame, payload_word);
    }
}

static void recordAnswer(TransactionRunner *self, const FrameMessage *frame)
{
    const TxnTemplateMessage *template_message = currentTemplate(self);

    keepRxFrame(self, frame);
    if (template_message->advance_mode != kTXN_ADVANCE_MODE_COUNT) {
        recordRosterAnswer(self, frame);
        return;
    }

    self->current.match_count++;
    self->current.satisfied = self->current.match_count >= template_message->count_k;
}

static void recordRosterAnswer(TransactionRunner *self, const FrameMessage *frame)
{
    const TxnTemplateMessage *template_message = currentTemplate(self);
    uint64_t roster_bit;
    uint8_t roster_index;
    uint8_t needed;

    if (!findRosterIndex(self, extractOrigin(self, frame), &roster_index)) {
        return;
    }
    roster_bit = (uint64_t)1 << roster_index;
    if (self->current.seen_bitmap & roster_bit) {
        return;
    }

    self->current.seen_bitmap |= roster_bit;
    self->current.seen_count++;
    needed = template_message->advance_mode == kTXN_ADVANCE_MODE_ROSTER_ALL ? self->roster_count : template_message->count_k;
    self->current.satisfied = self->current.seen_count >= needed;
}

static bool findRosterIndex(const TransactionRunner *self, uint32_t origin, uint8_t *roster_index)
{
    uint8_t i;

    for(i=0; i<self->roster_count; i++) {
        if (self->roster[i] == origin) {
            *roster_index = i;
            return true;
        }
    }
    return false;
}

static bool matchesId(const TxnPattern *pattern, uint32_t can_id)
{
    return (can_id & pattern->can_mask) == (pattern->can_id & pattern->can_mask);
}

static bool matchesPayload(uint64_t payload_word, uint64_t payload_mask, uint64_t payload_value)
{
    return (payload_word & payload_mask) == (payload_value & payload_mask);
}

static bool matchesPattern(const TxnPattern *pattern, const FrameMessage *frame, uint64_t payload_word)
{
    return matchesId(pattern, frame->can_id) && matchesPayload(payload_word, pattern->payload_mask, pattern->payload_value);
}

static bool matchesTemplatePattern(
    const TxnTemplateMessage *template_message,
    uint8_t pattern,
    const FrameMessage *frame,
    uint64_t payload_word
)
{
    if (!isPatternPresent(template_message, pattern)) {
        return false;
    }
    return matchesPattern(&template_message->patterns[pattern], frame, payload_word);
}

static void matchAdvance(
    TransactionRunner *self,
    const TxnTemplateMessage *template_message,
    const FrameMessage *frame,
    uint64_t payload_word
)
{
    const TxnPattern *advance = &template_message->patterns[kTXN_PATTERN_ADVANCE];
    uint64_t expected = expectedValue(self, template_message);

    if (!matchesId(advance, frame->can_id)) {
        return;
    }
    if (matchesPayload(payload_word, advance->payload_mask, expected)) {
        recordAnswer(self, frame);
        return;
    }
    if (template_message->flags & TXN_TEMPLATE_FLAG_MISMATCH_ABORT) {
        abortOnFrame(self, kTXN_REASON_VALUE_MISMATCH, frame, expected, payload_word);
    }
}

static uint64_t expectedValue(const TransactionRunner *self, const TxnTemplateMessage *template_message)
{
    if (self->current.step.flags & TXN_STEP_FLAG_EXPECT_VALUE) {
        return self->current.step.expect_value;
    }
    return template_message->patterns[kTXN_PATTERN_ADVANCE].payload_value;
}

static uint64_t payloadWord(const FrameMessage *frame)
{
    uint64_t word = 0;
    uint8_t i;

    for(i=0; i<TXN_PATTERN_PAYLOAD_BYTES && i<frame->payload_length; i++) {
        word |= (uint64_t)frame->payload[i] << (i * BITS_PER_BYTE);
    }
    return word;
}

static uint32_t payloadOrigin(const FrameMessage *frame, uint8_t offset)
{
    uint32_t value = 0;
    uint8_t i;

    for(i=0; i<ORIGIN_PAYLOAD_BYTES && offset + i < frame->payload_length; i++) {
        value |= (uint32_t)frame->payload[offset + i] << (i * BITS_PER_BYTE);
    }
    return value;
}

static uint32_t extractOrigin(const TransactionRunner *self, const FrameMessage *frame)
{
    uint32_t source_value;

    if (self->origin.source == kTXN_ORIGIN_SOURCE_NONE) {
        return 0;
    }
    if (self->origin.source == kTXN_ORIGIN_SOURCE_CAN_ID) {
        source_value = frame->can_id & FRAME_CAN_ID_MASK;
    } else {
        source_value = payloadOrigin(frame, self->origin.payload_offset);
    }
    return (source_value & self->origin.mask) >> self->origin.shift;
}

static void keepRxFrame(TransactionRunner *self, const FrameMessage *frame)
{
    self->current.has_rx_frame = true;
    self->current.rx_frame.can_id = frame->can_id;
    self->current.rx_frame.payload_length = frame->payload_length;
    self->current.rx_frame.frame_flags = frame->frame_flags;
    memcpy(self->current.rx_frame.payload, frame->payload, frame->payload_length);
}

static const TxnTemplateMessage *currentTemplate(const TransactionRunner *self)
{
    return &self->templates[self->current.step.template_id];
}

static uint64_t templateTimeoutUs(const TxnTemplateMessage *template_message)
{
    return (uint64_t)template_message->timeout_ms * MICROSECONDS_PER_MILLISECOND;
}

static uint64_t transmitTimeoutUs(const TxnTemplateMessage *template_message)
{
    if (template_message->timeout_ms == 0) {
        return (uint64_t)TRANSACTION_DEFAULT_TX_TIMEOUT_MS * MICROSECONDS_PER_MILLISECOND;
    }
    return templateTimeoutUs(template_message);
}

static uint64_t extendTimeoutUs(const TxnTemplateMessage *template_message)
{
    if (template_message->extend_timeout_ms == 0) {
        return templateTimeoutUs(template_message);
    }
    return (uint64_t)template_message->extend_timeout_ms * MICROSECONDS_PER_MILLISECOND;
}

static void prepareStatus(const TransactionRunner *self, TxnStatusMessage *status, uint8_t state, uint8_t reason)
{
    memset(status, 0, sizeof(*status));
    status->channel = self->channel;
    status->state = state;
    status->txn_id = self->txn_id;
    status->reason = reason;
    status->failing_seq = self->completed_steps;
    status->completed_steps = self->completed_steps;
    status->seen_bitmap = self->current.seen_bitmap;
    if (self->current.phase != kTRANSACTION_PHASE_IDLE && isRosterMode(currentTemplate(self)->advance_mode)) {
        status->flags |= TXN_STATUS_FLAG_ROSTER_STEP;
    }
    if (self->current.has_rx_frame) {
        status->flags |= TXN_STATUS_FLAG_HAS_RX_FRAME;
        status->rx_frame = self->current.rx_frame;
    }
}

static void finish(TransactionRunner *self, const TxnStatusMessage *status)
{
    self->active = false;
    StepRing_Clear(&self->ring);
    self->port.report_status(self->port.context, status);
}

static void abortWith(TransactionRunner *self, uint8_t reason, uint32_t failing_seq)
{
    TxnStatusMessage status;

    prepareStatus(self, &status, kTXN_STATE_ABORTED, reason);
    status.failing_seq = failing_seq;
    finish(self, &status);
}

static void abortOnFrame(TransactionRunner *self, uint8_t reason, const FrameMessage *frame, uint64_t expected, uint64_t got)
{
    TxnStatusMessage status;

    keepRxFrame(self, frame);
    prepareStatus(self, &status, kTXN_STATE_ABORTED, reason);
    status.offending_origin = extractOrigin(self, frame);
    status.expected = expected;
    status.got = got;
    finish(self, &status);
}

static uint64_t earliestWake(uint64_t current, uint64_t candidate)
{
    if (candidate == 0) {
        return current;
    }
    if (current == 0 || candidate < current) {
        return candidate;
    }
    return current;
}

static uint64_t transmitReadyUs(const TransactionRunner *self)
{
    uint64_t ready_us = self->next_transmit_us;

    if (self->current.retry_at_us > ready_us) {
        ready_us = self->current.retry_at_us;
    }
    if (ready_us < self->last_tick_us) {
        return self->last_tick_us;
    }
    return ready_us;
}
