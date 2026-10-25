#include "canhub.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client/client.h"
#include "client/transaction_builder.h"
#include "client/txn_script/checksum.h"
#include "platform/linux/clock/clock.h"
#include "platform/linux/libcanhub/canhub_frames.h"
#include "platform/linux/libcanhub/canhub_internal.h"

#define PUMP_SLICE_MS 50
#define FOREVER (-1)
#define MICROSECONDS_PER_MILLISECOND 1000
#define GRANT_TIMEOUT_US 5000000
#define REFUSAL_SIZE 256
#define BITS_PER_BYTE 8

struct CanHubTxn {
    CanHubSession *session;
    TransactionBuilder builder;
    TransactionBuild build;
    TxnBeginMessage begin;
    CanHubTxnRosterCallback on_roster;
    void *context;
    bool begin_sent;
    uint64_t grant_deadline_us;
    bool began;
    bool refused;
    int32_t refusal;
    char refusal_message[REFUSAL_SIZE];
    bool in_callback;
    uint32_t steps;
    atomic_bool abort_requested;
    bool abort_sent;
    bool detached;
    bool has_isotp_step;
    uint32_t isotp_step;
};

static const char *const REASON_NAMES[] = {
    "none",
    "timeout",
    "failure_matched",
    "value_mismatch",
    "tx_failed",
    "cancelled",
    "starved",
    "overrun",
    "agent_disconnected",
    "invalid_step",
    "flow_control_timeout",
    "consecutive_frame_timeout",
    "overflow",
    "sequence_error",
    "wait_limit",
    "unsupported",
};

static const uint8_t EMPTY_FRAME[1] = { 0 };

static Client *clientOf(const CanHubTxn *self);
static const TransactionSender *senderOf(const CanHubTxn *self);
static bool isBeginning(const CanHubTxn *self);
static bool isRunning(const CanHubTxn *self);
static bool isFinished(const CanHubTxn *self);
static int32_t refuse(CanHubTxn *self, int32_t result, const char *message);
static int32_t refuseStep(CanHubTxn *self, const char *reason);
static int32_t checkCallable(CanHubTxn *self);
static void serviceAbort(CanHubTxn *self);
static int32_t awaitProgress(CanHubTxn *self);
static uint64_t deadlineFrom(int32_t timeout_ms);
static bool hasExpired(uint64_t deadline_us);
static int32_t endedResult(CanHubTxn *self);
static int32_t queueStep(CanHubTxn *self, const CanHubTxnStep *step, uint64_t deadline_us);
static int32_t awaitGrant(CanHubTxn *self, uint64_t deadline_us);
static int32_t beginResult(uint8_t begin_status);
static const char *beginDetail(uint8_t begin_status);
static int32_t awaitRoom(CanHubTxn *self, uint64_t deadline_us);
static bool hasRoom(CanHubTxn *self);
static bool hasCreditFor(const TransactionSender *sender, const TxnStep *records, uint8_t count);
static void noteIsotpStep(CanHubTxn *self);
static int32_t defineRoster(CanHubTxn *self);
static int32_t defineTemplate(CanHubTxn *self);
static int32_t handOverRecords(CanHubTxn *self);
static int32_t flushSteps(CanHubTxn *self);
static int32_t endTransaction(CanHubTxn *self);
static int32_t awaitOutcome(CanHubTxn *self);
static void fillReport(const CanHubTxn *self, CanHubTxnReport *report);
static void fillMissing(const CanHubTxn *self, uint32_t step, uint64_t seen_bitmap, CanHubTxnRosterResult *result);
static void wordToBytes(uint64_t word, uint8_t *bytes);
static void release(CanHubTxn *self);
static uint32_t checksumOf(const ChecksumModel *model, const uint8_t *data, uint32_t length);

/* ---------- public ---------- */

CanHubTxn *canhub_txn_create(CanHubSession *session, const CanHubTxnOptions *options)
{
    CanHubTxn *self;
    TxnBeginMessage begin;
    const char *error = NULL;

    if (session == NULL) {
        return NULL;
    }
    if (CanHubSession_Transaction(session) != NULL) {
        CanHubSession_SetError(session, "a transaction already runs on this session: finish it first");
        return NULL;
    }
    if (Client_State(CanHubSession_Client(session)) != kCLIENT_OPEN) {
        CanHubSession_SetError(session, "a transaction needs an open interface");
        return NULL;
    }
    if (!TransactionBuilder_Begin(options, &begin, &error)) {
        CanHubSession_SetError(session, error);
        return NULL;
    }

    self = calloc(1, sizeof(*self));
    if (self == NULL) {
        CanHubSession_SetError(session, "out of memory for the transaction");
        return NULL;
    }
    self->session = session;
    self->begin = begin;
    TransactionBuilder_Init(&self->builder, options != NULL && options->bitrate_switch);
    if (options != NULL) {
        self->on_roster = options->on_roster;
        self->context = options->context;
    }
    atomic_init(&self->abort_requested, false);
    CanHubSession_SetTransaction(session, self);
    return self;
}

int32_t canhub_txn_send(
    CanHubTxn *transaction,
    CanHubCanId id,
    const uint8_t *data,
    uint8_t length,
    const CanHubTxnResponse *response
)
{
    CanHubTxnStep step;

    memset(&step, 0, sizeof(step));
    step.id = id;
    step.data = data != NULL ? data : EMPTY_FRAME;
    step.length = data != NULL ? length : 0;
    step.response = response;
    return canhub_txn_step(transaction, &step);
}

int32_t canhub_txn_send_message(
    CanHubTxn *transaction,
    const CanHubTxnIsotpLink *link,
    const uint8_t *data,
    uint16_t length,
    const CanHubTxnResponse *response
)
{
    CanHubTxnStep step;

    if (transaction == NULL) {
        return CANHUB_ERR_ARGUMENT;
    }
    if (link == NULL || data == NULL) {
        return refuseStep(transaction, "an ISO-TP step needs its link and its message");
    }

    memset(&step, 0, sizeof(step));
    step.id = link->tx_id;
    step.data = data;
    step.length = length;
    step.isotp = link;
    step.response = response;
    return canhub_txn_step(transaction, &step);
}

int32_t canhub_txn_expect(CanHubTxn *transaction, const CanHubTxnResponse *response)
{
    CanHubTxnStep step;

    memset(&step, 0, sizeof(step));
    step.response = response;
    return canhub_txn_step(transaction, &step);
}

int32_t canhub_txn_step(CanHubTxn *transaction, const CanHubTxnStep *step)
{
    return canhub_txn_try_step(transaction, step, FOREVER);
}

int32_t canhub_txn_try_step(CanHubTxn *transaction, const CanHubTxnStep *step, int32_t timeout_ms)
{
    int32_t result = CanHubTxn_Queue(transaction, step, timeout_ms);

    if (result != CANHUB_OK) {
        return result;
    }
    return flushSteps(transaction);
}

int32_t canhub_txn_progress(CanHubTxn *transaction, CanHubTxnProgress *progress)
{
    const TransactionSender *sender;

    if (transaction == NULL || progress == NULL) {
        return CANHUB_ERR_ARGUMENT;
    }

    memset(progress, 0, sizeof(*progress));
    if (transaction->detached) {
        return CANHUB_ERR_DISCONNECTED;
    }
    if (!transaction->began) {
        return CANHUB_OK;
    }
    sender = senderOf(transaction);
    progress->sent_steps = sender->next_seq;
    progress->completed_steps = sender->completed_steps;
    progress->bytes_in_flight = TransactionSender_BytesInFlight(sender);
    progress->window_bytes = sender->window_bytes;
    return CANHUB_OK;
}

int32_t canhub_txn_abort(CanHubTxn *transaction)
{
    if (transaction == NULL) {
        return CANHUB_ERR_ARGUMENT;
    }

    atomic_store(&transaction->abort_requested, true);
    return CANHUB_OK;
}

int32_t canhub_txn_wait(CanHubTxn *transaction, int32_t timeout_ms)
{
    uint64_t deadline_us = deadlineFrom(timeout_ms);
    int32_t result;

    if (transaction == NULL) {
        return CANHUB_ERR_ARGUMENT;
    }
    if (transaction->detached) {
        return CANHUB_ERR_DISCONNECTED;
    }
    if (transaction->in_callback) {
        CanHubSession_SetError(transaction->session, "on_roster must not call the transaction");
        return CANHUB_ERR_STATE;
    }

    result = endTransaction(transaction);
    while (result == CANHUB_OK && transaction->began && !transaction->refused && !isFinished(transaction)) {
        if (hasExpired(deadline_us)) {
            return CANHUB_ERR_TIMEOUT;
        }
        result = awaitProgress(transaction);
    }
    return result == CANHUB_ERR_DISCONNECTED ? result : CANHUB_OK;
}

int32_t canhub_txn_finish(CanHubTxn *transaction, CanHubTxnReport *report)
{
    CanHubTxnReport local_report;
    int32_t result;

    if (transaction == NULL) {
        return CANHUB_ERR_ARGUMENT;
    }
    if (report == NULL) {
        report = &local_report;
    }
    if (transaction->detached) {
        fillReport(transaction, report);
        free(transaction);
        return CANHUB_ERR_DISCONNECTED;
    }
    if (transaction->in_callback) {
        CanHubSession_SetError(transaction->session, "on_roster must not call the transaction");
        return CANHUB_ERR_STATE;
    }

    result = endTransaction(transaction);
    if (result == CANHUB_OK) {
        result = awaitOutcome(transaction);
    }
    fillReport(transaction, report);
    if (transaction->refused) {
        CanHubSession_SetError(transaction->session, transaction->refusal_message);
        result = transaction->refusal;
    }

    release(transaction);
    return result;
}

const char *canhub_txn_reason_name(CanHubTxnReason reason)
{
    if ((uint32_t)reason >= sizeof(REASON_NAMES) / sizeof(REASON_NAMES[0])) {
        return "unknown";
    }
    return REASON_NAMES[reason];
}

uint8_t canhub_txn_sum8(const uint8_t *data, uint32_t length)
{
    return (uint8_t)checksumOf(&CHECKSUM_SUM8, data, length);
}

uint16_t canhub_txn_sum16(const uint8_t *data, uint32_t length)
{
    return (uint16_t)checksumOf(&CHECKSUM_SUM16, data, length);
}

uint32_t canhub_txn_sum32(const uint8_t *data, uint32_t length)
{
    return checksumOf(&CHECKSUM_SUM32, data, length);
}

uint16_t canhub_txn_crc16(const uint8_t *data, uint32_t length)
{
    return (uint16_t)checksumOf(&CHECKSUM_CRC16, data, length);
}

uint32_t canhub_txn_crc32(const uint8_t *data, uint32_t length)
{
    return checksumOf(&CHECKSUM_CRC32, data, length);
}

uint32_t canhub_txn_crc(const CanHubTxnCrc *crc, const uint8_t *data, uint32_t length)
{
    ChecksumModel model;

    if (crc == NULL) {
        return 0;
    }

    model.kind = kCHECKSUM_KIND_CRC;
    model.width = crc->width;
    model.polynomial = crc->polynomial;
    model.initial = crc->initial;
    model.reflect_input = crc->reflect_input;
    model.reflect_output = crc->reflect_output;
    model.final_xor = crc->final_xor;
    if (!ChecksumModel_IsValid(&model)) {
        return 0;
    }
    return checksumOf(&model, data, length);
}

int32_t CanHubTxn_Queue(CanHubTxn *transaction, const CanHubTxnStep *step, int32_t timeout_ms)
{
    int32_t result;

    if (transaction == NULL) {
        return CANHUB_ERR_ARGUMENT;
    }
    result = checkCallable(transaction);
    if (result != CANHUB_OK) {
        return result;
    }
    if (step == NULL) {
        return refuseStep(transaction, "step required");
    }
    return queueStep(transaction, step, deadlineFrom(timeout_ms));
}

void CanHubTxn_OnRosterReport(CanHubTxn *transaction, const TxnRosterReportMessage *report)
{
    CanHubTxnRosterResult result;

    if (transaction == NULL || transaction->on_roster == NULL || !transaction->builder.has_roster) {
        return;
    }

    fillMissing(transaction, report->seq, report->seen_bitmap, &result);
    transaction->in_callback = true;
    transaction->on_roster(transaction->context, &result);
    transaction->in_callback = false;
}

void CanHubTxn_Detach(CanHubTxn *transaction)
{
    if ((isRunning(transaction) || isBeginning(transaction)) && !transaction->abort_sent) {
        Client_AbortTransaction(clientOf(transaction));
        transaction->abort_sent = true;
        CanHubSession_Pump(transaction->session, 0);
    }
    atomic_store(&transaction->abort_requested, true);
    transaction->detached = true;
    transaction->session = NULL;
}

/* ---------- private ---------- */

static Client *clientOf(const CanHubTxn *self)
{
    return CanHubSession_Client(self->session);
}

static const TransactionSender *senderOf(const CanHubTxn *self)
{
    return Client_Transaction(clientOf(self));
}

static bool isBeginning(const CanHubTxn *self)
{
    return self->begin_sent && !self->began && senderOf(self)->state == kTRANSACTION_SENDER_BEGINNING;
}

static bool isRunning(const CanHubTxn *self)
{
    return self->began && senderOf(self)->state == kTRANSACTION_SENDER_RUNNING;
}

static bool isFinished(const CanHubTxn *self)
{
    return self->began && senderOf(self)->state == kTRANSACTION_SENDER_FINISHED;
}

static int32_t refuse(CanHubTxn *self, int32_t result, const char *message)
{
    self->refused = true;
    self->refusal = result;
    snprintf(self->refusal_message, sizeof(self->refusal_message), "%s", message);
    CanHubSession_SetError(self->session, self->refusal_message);
    if (isRunning(self) || isBeginning(self)) {
        Client_AbortTransaction(clientOf(self));
        self->abort_sent = true;
    }
    return result;
}

static int32_t refuseStep(CanHubTxn *self, const char *reason)
{
    char message[REFUSAL_SIZE];

    snprintf(message, sizeof(message), "step %" PRIu32 ": %s", self->steps, reason);
    return refuse(self, CANHUB_ERR_ARGUMENT, message);
}

static int32_t checkCallable(CanHubTxn *self)
{
    if (self->detached) {
        return CANHUB_ERR_DISCONNECTED;
    }
    if (self->in_callback) {
        CanHubSession_SetError(self->session, "on_roster must not call the transaction");
        return CANHUB_ERR_STATE;
    }
    if (self->refused) {
        CanHubSession_SetError(self->session, self->refusal_message);
        return self->refusal;
    }
    serviceAbort(self);
    if (atomic_load(&self->abort_requested) || isFinished(self)) {
        return endedResult(self);
    }
    return CANHUB_OK;
}

static void serviceAbort(CanHubTxn *self)
{
    if (self->abort_sent || !atomic_load(&self->abort_requested) || !isRunning(self)) {
        return;
    }

    Client_AbortTransaction(clientOf(self));
    self->abort_sent = true;
    CanHubSession_Pump(self->session, 0);
}

static int32_t awaitProgress(CanHubTxn *self)
{
    serviceAbort(self);
    if (CanHubSession_IsDisconnected(self->session)) {
        CanHubSession_SetError(self->session, "connection lost");
        return CANHUB_ERR_DISCONNECTED;
    }
    if (isFinished(self)) {
        return endedResult(self);
    }

    Client_FlushTransaction(clientOf(self));
    CanHubSession_Pump(self->session, PUMP_SLICE_MS);
    Client_FlushTransaction(clientOf(self));
    return CANHUB_OK;
}

static uint64_t deadlineFrom(int32_t timeout_ms)
{
    if (timeout_ms < 0) {
        return UINT64_MAX;
    }
    return Clock_MonotonicUs() + (uint64_t)timeout_ms * MICROSECONDS_PER_MILLISECOND;
}

static bool hasExpired(uint64_t deadline_us)
{
    return deadline_us != UINT64_MAX && Clock_MonotonicUs() >= deadline_us;
}

static int32_t endedResult(CanHubTxn *self)
{
    const TxnStatusMessage *outcome = &senderOf(self)->outcome;

    if (isFinished(self) && outcome->state == kTXN_STATE_DONE && outcome->reason == kTXN_REASON_NONE) {
        return CANHUB_OK;
    }
    CanHubSession_SetError(self->session, "the transaction was aborted: canhub_txn_finish reports why");
    return CANHUB_ERR_TXN_ABORTED;
}

static int32_t queueStep(CanHubTxn *self, const CanHubTxnStep *step, uint64_t deadline_us)
{
    TransactionBuilderMark mark = TransactionBuilder_Mark(&self->builder);
    int32_t result;

    if (!TransactionBuilder_Step(&self->builder, step, &self->build)) {
        return refuseStep(self, TransactionBuilder_Error(&self->builder));
    }
    result = awaitGrant(self, deadline_us);
    if (result == CANHUB_OK) {
        result = awaitRoom(self, deadline_us);
    }
    if (result == CANHUB_ERR_TIMEOUT && !self->refused) {
        TransactionBuilder_Rewind(&self->builder, &mark);
    }
    if (result != CANHUB_OK) {
        return result;
    }

    noteIsotpStep(self);
    if (self->build.defines_roster) {
        result = defineRoster(self);
    }
    if (result == CANHUB_OK && self->build.defines_template) {
        result = defineTemplate(self);
    }
    if (result == CANHUB_OK) {
        result = handOverRecords(self);
    }
    if (result == CANHUB_OK) {
        self->steps++;
    }
    return result;
}

static int32_t awaitGrant(CanHubTxn *self, uint64_t deadline_us)
{
    uint8_t status;

    if (self->began) {
        return CANHUB_OK;
    }
    if (!self->begin_sent) {
        if (!Client_BeginTransaction(clientOf(self), &self->begin)) {
            return refuse(self, CANHUB_ERR_STATE, "a transaction needs an open interface and none running");
        }
        self->begin_sent = true;
        self->grant_deadline_us = Clock_MonotonicUs() + GRANT_TIMEOUT_US;
    }

    while (senderOf(self)->state == kTRANSACTION_SENDER_BEGINNING) {
        if (CanHubSession_IsDisconnected(self->session)) {
            return refuse(self, CANHUB_ERR_DISCONNECTED, "connection lost");
        }
        if (Clock_MonotonicUs() >= self->grant_deadline_us) {
            return refuse(self, CANHUB_ERR_AGENT_UNREACHABLE, "the agent did not grant the transaction in time");
        }
        if (hasExpired(deadline_us)) {
            CanHubSession_SetError(self->session, "the agent has not granted the transaction yet: nothing of the step was sent");
            return CANHUB_ERR_TIMEOUT;
        }
        CanHubSession_Pump(self->session, PUMP_SLICE_MS);
    }

    status = senderOf(self)->begin_status;
    if (status != kTXN_BEGIN_STATUS_OK) {
        return refuse(self, beginResult(status), beginDetail(status));
    }
    self->began = true;
    if (senderOf(self)->state != kTRANSACTION_SENDER_RUNNING) {
        return endedResult(self);
    }
    return CANHUB_OK;
}

static int32_t beginResult(uint8_t begin_status)
{
    if (begin_status == kTXN_BEGIN_STATUS_BUSY) {
        return CANHUB_ERR_TXN_BUSY;
    }
    if (begin_status == kTXN_BEGIN_STATUS_WRITE_DENIED) {
        return CANHUB_ERR_WRITE_DENIED;
    }
    if (begin_status == kTXN_BEGIN_STATUS_UNSUPPORTED) {
        return CANHUB_ERR_TXN_UNSUPPORTED;
    }
    if (begin_status == kTXN_BEGIN_STATUS_RELIABLE_CHANNEL_REQUIRED) {
        return CANHUB_ERR_STATE;
    }
    return CANHUB_ERR_ARGUMENT;
}

static const char *beginDetail(uint8_t begin_status)
{
    if (begin_status == kTXN_BEGIN_STATUS_BUSY) {
        return "transaction refused: busy, the agent runs another transaction";
    }
    if (begin_status == kTXN_BEGIN_STATUS_WRITE_DENIED) {
        return "transaction refused: write denied, no write grant on this interface";
    }
    if (begin_status == kTXN_BEGIN_STATUS_UNSUPPORTED) {
        return "transaction refused: the agent does not run transactions";
    }
    if (begin_status == kTXN_BEGIN_STATUS_RELIABLE_CHANNEL_REQUIRED) {
        return "transaction refused: open the interface reliable";
    }
    return "transaction refused: invalid request";
}

static int32_t awaitRoom(CanHubTxn *self, uint64_t deadline_us)
{
    int32_t result;

    while (!hasRoom(self)) {
        if (hasExpired(deadline_us)) {
            CanHubSession_SetError(self->session, "the agent's window did not take the step in time: nothing of it was sent");
            return CANHUB_ERR_TIMEOUT;
        }
        result = awaitProgress(self);
        if (result != CANHUB_OK) {
            return result;
        }
    }
    return CANHUB_OK;
}

static bool hasRoom(CanHubTxn *self)
{
    bool defines = self->build.defines_roster || self->build.defines_template;

    if (defines && !Client_FlushTransaction(clientOf(self))) {
        return false;
    }
    return hasCreditFor(senderOf(self), self->build.records, self->build.record_count);
}

static bool hasCreditFor(const TransactionSender *sender, const TxnStep *records, uint8_t count)
{
    uint32_t bytes = 0;
    uint8_t i;

    for(i=0; i<count; i++) {
        bytes += (uint32_t)TxnStep_RecordSize(&records[i]);
    }
    return TransactionSender_BytesInFlight(sender) + bytes <= sender->window_bytes;
}

static void noteIsotpStep(CanHubTxn *self)
{
    if (self->has_isotp_step || !self->build.defines_template) {
        return;
    }
    if (self->build.template_message.type == kTXN_TEMPLATE_TYPE_ISOTP) {
        self->has_isotp_step = true;
        self->isotp_step = self->steps;
    }
}

static int32_t defineRoster(CanHubTxn *self)
{
    int32_t result;

    while (!Client_DefineRoster(clientOf(self), &self->build.roster)) {
        result = awaitProgress(self);
        if (result != CANHUB_OK) {
            return result;
        }
    }
    return CANHUB_OK;
}

static int32_t defineTemplate(CanHubTxn *self)
{
    int32_t result;

    while (!Client_DefineTemplate(clientOf(self), &self->build.template_message)) {
        result = awaitProgress(self);
        if (result != CANHUB_OK) {
            return result;
        }
    }
    return CANHUB_OK;
}

static int32_t handOverRecords(CanHubTxn *self)
{
    TTRANSACTION_OFFER stopped_by;
    uint8_t count = self->build.record_count;
    uint8_t taken = 0;
    int32_t result;

    while (taken < count) {
        taken = (uint8_t)(taken + Client_SendSteps(clientOf(self), self->build.records + taken, count - taken, &stopped_by));
        if (taken == count) {
            break;
        }
        if (stopped_by == kTRANSACTION_OFFER_REJECTED) {
            return refuseStep(self, "the step does not fit the wire");
        }
        result = awaitProgress(self);
        if (result != CANHUB_OK) {
            return result;
        }
    }
    return CANHUB_OK;
}

static int32_t flushSteps(CanHubTxn *self)
{
    int32_t result;

    while (!Client_FlushTransaction(clientOf(self))) {
        result = awaitProgress(self);
        if (result != CANHUB_OK) {
            return result;
        }
    }
    CanHubSession_Pump(self->session, 0);
    return CANHUB_OK;
}

static int32_t endTransaction(CanHubTxn *self)
{
    serviceAbort(self);
    if (!isRunning(self) || self->abort_sent || senderOf(self)->ended) {
        return CANHUB_OK;
    }

    Client_EndTransaction(clientOf(self));
    return flushSteps(self) == CANHUB_ERR_DISCONNECTED ? CANHUB_ERR_DISCONNECTED : CANHUB_OK;
}

static int32_t awaitOutcome(CanHubTxn *self)
{
    int32_t result;

    if (!self->began) {
        if (atomic_load(&self->abort_requested)) {
            return CANHUB_ERR_TXN_ABORTED;
        }
        return CANHUB_OK;
    }
    while (!isFinished(self)) {
        result = awaitProgress(self);
        if (result != CANHUB_OK) {
            return result;
        }
    }
    return endedResult(self);
}

static void fillReport(const CanHubTxn *self, CanHubTxnReport *report)
{
    const TxnStatusMessage *outcome;

    memset(report, 0, sizeof(*report));
    if (self->detached || !isFinished(self)) {
        report->done = !self->began && !self->refused && !atomic_load(&self->abort_requested);
        report->reason = atomic_load(&self->abort_requested) ? CANHUB_TXN_REASON_CANCELLED : CANHUB_TXN_REASON_NONE;
        report->failing_step = self->steps;
        return;
    }

    outcome = &senderOf(self)->outcome;
    report->done = outcome->state == kTXN_STATE_DONE && outcome->reason == kTXN_REASON_NONE;
    report->reason = (CanHubTxnReason)outcome->reason;
    report->failing_step = outcome->failing_seq;
    if (report->reason == CANHUB_TXN_REASON_UNSUPPORTED && self->has_isotp_step) {
        report->failing_step = self->isotp_step;
    }
    report->completed_steps = outcome->completed_steps;
    report->offending_node = outcome->offending_origin;
    wordToBytes(outcome->expected, report->expected);
    wordToBytes(outcome->got, report->got);
    if (!report->done && (outcome->flags & TXN_STATUS_FLAG_ROSTER_STEP) != 0) {
        fillMissing(self, outcome->failing_seq, outcome->seen_bitmap, &report->failing_roster);
    }
    report->has_frame = (outcome->flags & TXN_STATUS_FLAG_HAS_RX_FRAME) != 0;
    if (report->has_frame) {
        CanHubFrames_FromRxFrame(&outcome->rx_frame, &report->frame);
    }
}

static void fillMissing(const CanHubTxn *self, uint32_t step, uint64_t seen_bitmap, CanHubTxnRosterResult *result)
{
    const TxnRosterMessage *roster = &self->builder.roster;
    uint8_t i;

    memset(result, 0, sizeof(*result));
    result->step = step;
    for(i=0; i<roster->roster_count; i++) {
        if ((seen_bitmap & ((uint64_t)1 << i)) == 0) {
            result->missing[result->missing_count++] = roster->roster[i];
        }
    }
}

static void wordToBytes(uint64_t word, uint8_t *bytes)
{
    uint8_t i;

    for(i=0; i<CANHUB_TXN_MATCH_BYTES; i++) {
        bytes[i] = (uint8_t)(word >> (i * BITS_PER_BYTE));
    }
}

static void release(CanHubTxn *self)
{
    if (CanHubSession_Transaction(self->session) == self) {
        CanHubSession_SetTransaction(self->session, NULL);
    }
    free(self);
}

static uint32_t checksumOf(const ChecksumModel *model, const uint8_t *data, uint32_t length)
{
    Checksum checksum;

    Checksum_Start(&checksum, model);
    if (data != NULL) {
        Checksum_Update(&checksum, data, length);
    }
    return Checksum_Finish(&checksum);
}
