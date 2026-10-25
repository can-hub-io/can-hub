#include "client/transaction_sender.h"

#include <string.h>

static bool isStepValid(const TxnStep *step);
static void openBatch(TransactionSender *self);

/* ---------- public ---------- */

void TransactionSender_Reset(TransactionSender *self)
{
    memset(self, 0, sizeof(*self));
}

void TransactionSender_Begin(TransactionSender *self, uint8_t channel)
{
    TransactionSender_Reset(self);
    self->state = kTRANSACTION_SENDER_BEGINNING;
    self->channel = channel;
}

void TransactionSender_OnBeginAck(TransactionSender *self, const TxnBeginAckMessage *ack)
{
    if (self->state != kTRANSACTION_SENDER_BEGINNING || ack->channel != self->channel) {
        return;
    }

    self->begin_status = ack->status;
    if (ack->status != kTXN_BEGIN_STATUS_OK) {
        self->state = kTRANSACTION_SENDER_FINISHED;
        return;
    }

    self->state = kTRANSACTION_SENDER_RUNNING;
    self->txn_id = ack->txn_id;
    self->window_bytes = ack->window_bytes;
}

void TransactionSender_OnProgress(TransactionSender *self, const TxnProgressMessage *progress)
{
    if (self->state != kTRANSACTION_SENDER_RUNNING || progress->channel != self->channel || progress->txn_id != self->txn_id) {
        return;
    }

    self->consumed_bytes = progress->consumed_bytes;
    self->completed_steps = progress->completed_steps;
}

void TransactionSender_OnStatus(TransactionSender *self, const TxnStatusMessage *status)
{
    if (status->channel != self->channel) {
        return;
    }
    if (self->state == kTRANSACTION_SENDER_RUNNING && status->txn_id != self->txn_id) {
        return;
    }
    if (self->state != kTRANSACTION_SENDER_RUNNING && self->state != kTRANSACTION_SENDER_BEGINNING) {
        return;
    }

    self->outcome = *status;
    self->completed_steps = status->completed_steps;
    self->state = kTRANSACTION_SENDER_FINISHED;
}

TTRANSACTION_OFFER TransactionSender_Offer(TransactionSender *self, const TxnStep *step)
{
    uint32_t size;

    if (self->state != kTRANSACTION_SENDER_RUNNING || self->ended || !isStepValid(step)) {
        return kTRANSACTION_OFFER_REJECTED;
    }

    size = (uint32_t)TxnStep_RecordSize(step);
    if (TransactionSender_BytesInFlight(self) + size > self->window_bytes) {
        return kTRANSACTION_OFFER_NO_CREDIT;
    }
    if (!self->batch_open) {
        openBatch(self);
    }
    if (!TxnStepsWriter_Append(&self->writer, step)) {
        return kTRANSACTION_OFFER_FLUSH_NEEDED;
    }

    self->sent_bytes += size;
    self->message_open = (step->flags & TXN_STEP_FLAG_CONTINUED) != 0;
    if (!self->message_open) {
        self->next_seq++;
    }

    return kTRANSACTION_OFFER_ACCEPTED;
}

TTRANSACTION_OFFER TransactionSender_End(TransactionSender *self)
{
    if (self->state != kTRANSACTION_SENDER_RUNNING || self->ended || self->message_open) {
        return kTRANSACTION_OFFER_REJECTED;
    }

    if (!self->batch_open) {
        openBatch(self);
    }
    TxnStepsWriter_MarkEnd(&self->writer);
    self->ended = true;

    return kTRANSACTION_OFFER_ACCEPTED;
}

size_t TransactionSender_PendingBatch(TransactionSender *self, const uint8_t **data)
{
    if (!self->batch_open) {
        return 0;
    }

    *data = self->batch;

    return TxnStepsWriter_Finish(&self->writer);
}

void TransactionSender_BatchSent(TransactionSender *self)
{
    self->batch_open = false;
}

uint32_t TransactionSender_BytesInFlight(const TransactionSender *self)
{
    return self->sent_bytes - self->consumed_bytes;
}

/* ---------- private ---------- */

static bool isStepValid(const TxnStep *step)
{
    uint8_t record[TXN_STEP_RECORD_SIZE_MAX];

    return TxnStep_Encode(step, record, sizeof(record)) != 0;
}

static void openBatch(TransactionSender *self)
{
    TxnStepsMessage fixed_fields;

    memset(&fixed_fields, 0, sizeof(fixed_fields));
    fixed_fields.channel = self->channel;
    fixed_fields.txn_id = self->txn_id;
    fixed_fields.first_seq = self->next_seq;
    TxnStepsWriter_Init(&self->writer, &fixed_fields, self->batch, sizeof(self->batch));
    self->batch_open = true;
}
