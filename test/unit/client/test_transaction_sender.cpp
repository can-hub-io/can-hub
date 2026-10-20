#include <cest>
#include <cstring>

extern "C" {
#include "client/transaction_sender.h"
}

#define CHANNEL 3
#define OTHER_CHANNEL 4
#define TXN_ID 7
#define CLASSIC_RECORD_SIZE 16
#define TWO_RECORDS_WINDOW (2 * CLASSIC_RECORD_SIZE)
#define LARGE_WINDOW (48 * 1024)
#define RECORDS_PER_BATCH (TXN_STEPS_RECORD_BYTES_MAX / CLASSIC_RECORD_SIZE)

static TransactionSender sender;

static void grant(uint32_t window_bytes);
static TxnStep classicStep(uint32_t can_id);
static uint32_t offerUntilStopped(uint32_t count, TTRANSACTION_OFFER *stopped_by);
static TxnStepsMessage pendingBatch(void);
static TxnStep decodedStep(const TxnStepsMessage *batch, uint8_t index);

describe("transaction_sender", []() {
    beforeEach([]() {
        TransactionSender_Begin(&sender, CHANNEL);
    });

    it("runs once the agent grants the transaction, with its window", []() {
        grant(LARGE_WINDOW);

        expect(sender.state).toBe((uint8_t)kTRANSACTION_SENDER_RUNNING);
        expect(sender.txn_id).toBe((uint16_t)TXN_ID);
        expect(sender.window_bytes).toBe((uint32_t)LARGE_WINDOW);
    });

    it("finishes with the refusal when the begin is refused", []() {
        TxnBeginAckMessage ack = { CHANNEL, kTXN_BEGIN_STATUS_BUSY, 0, 0 };

        TransactionSender_OnBeginAck(&sender, &ack);

        expect(sender.state).toBe((uint8_t)kTRANSACTION_SENDER_FINISHED);
        expect(sender.begin_status).toBe((uint8_t)kTXN_BEGIN_STATUS_BUSY);
    });

    it("ignores a grant for another channel", []() {
        TxnBeginAckMessage ack = { OTHER_CHANNEL, kTXN_BEGIN_STATUS_OK, TXN_ID, LARGE_WINDOW };

        TransactionSender_OnBeginAck(&sender, &ack);

        expect(sender.state).toBe((uint8_t)kTRANSACTION_SENDER_BEGINNING);
    });

    it("holds the last offered step back", []() {
        TxnStep step;
        const uint8_t *batch;
        size_t held_only_size;
        TxnStepsMessage sent;

        grant(LARGE_WINDOW);
        step = classicStep(0x100);
        TransactionSender_Offer(&sender, &step);
        held_only_size = TransactionSender_PendingBatch(&sender, &batch);
        step = classicStep(0x101);

        TransactionSender_Offer(&sender, &step);
        sent = pendingBatch();

        expect(held_only_size).toBe((size_t)0);
        expect(sent.step_count).toBe(1);
        expect(decodedStep(&sent, 0).can_id).toBe((uint32_t)0x100);
    });

    it("flags the held step last when the transaction ends", []() {
        TxnStep step;
        TTRANSACTION_OFFER ended;
        TxnStepsMessage sent;

        grant(LARGE_WINDOW);
        step = classicStep(0x100);
        TransactionSender_Offer(&sender, &step);

        ended = TransactionSender_End(&sender);
        sent = pendingBatch();

        expect(ended).toBe(kTRANSACTION_OFFER_ACCEPTED);
        expect(sent.step_count).toBe(1);
        expect(decodedStep(&sent, 0).flags).toBe((uint8_t)TXN_STEP_FLAG_LAST);
    });

    it("refuses to end a transaction that has sent nothing", []() {
        grant(LARGE_WINDOW);

        expect(TransactionSender_End(&sender)).toBe(kTRANSACTION_OFFER_REJECTED);
    });

    it("stops when the window is spent and resumes as credit comes back", []() {
        TxnProgressMessage progress = { CHANNEL, TXN_ID, 1, CLASSIC_RECORD_SIZE };
        TTRANSACTION_OFFER stopped_by;
        TxnStep step;
        uint32_t accepted;
        TTRANSACTION_OFFER resumed;

        grant(TWO_RECORDS_WINDOW);
        accepted = offerUntilStopped(4, &stopped_by);

        TransactionSender_OnProgress(&sender, &progress);
        step = classicStep(0x200);
        resumed = TransactionSender_Offer(&sender, &step);

        expect(accepted).toBe((uint32_t)3);
        expect(stopped_by).toBe(kTRANSACTION_OFFER_NO_CREDIT);
        expect(resumed).toBe(kTRANSACTION_OFFER_ACCEPTED);
        expect(TransactionSender_BytesInFlight(&sender)).toBe((uint32_t)TWO_RECORDS_WINDOW);
    });

    it("asks for a flush once a batch is full and numbers the next one on", []() {
        TTRANSACTION_OFFER stopped_by;
        TxnStep step;
        TxnStepsMessage first;
        TxnStepsMessage second;

        grant(LARGE_WINDOW);
        offerUntilStopped(RECORDS_PER_BATCH + 2, &stopped_by);
        first = pendingBatch();
        TransactionSender_BatchSent(&sender);
        step = classicStep(0x300);
        TransactionSender_Offer(&sender, &step);
        second = pendingBatch();

        expect(stopped_by).toBe(kTRANSACTION_OFFER_FLUSH_NEEDED);
        expect(first.step_count).toBe(RECORDS_PER_BATCH);
        expect(first.first_seq).toBe((uint32_t)0);
        expect(second.first_seq).toBe((uint32_t)RECORDS_PER_BATCH);
    });

    it("rejects an invalid step", []() {
        TxnStep step;

        grant(LARGE_WINDOW);
        step = classicStep(0x100);
        step.payload_length = FRAME_PAYLOAD_MAX_CLASSIC + 1;

        expect(TransactionSender_Offer(&sender, &step)).toBe(kTRANSACTION_OFFER_REJECTED);
    });

    it("finishes with the agent's outcome", []() {
        TxnStatusMessage status;

        grant(LARGE_WINDOW);
        memset(&status, 0, sizeof(status));
        status.channel = CHANNEL;
        status.txn_id = TXN_ID;
        status.state = kTXN_STATE_ABORTED;
        status.reason = kTXN_REASON_TIMEOUT;
        status.completed_steps = 12;

        TransactionSender_OnStatus(&sender, &status);

        expect(sender.state).toBe((uint8_t)kTRANSACTION_SENDER_FINISHED);
        expect(sender.outcome.reason).toBe((uint8_t)kTXN_REASON_TIMEOUT);
        expect(sender.completed_steps).toBe((uint32_t)12);
    });

    it("ignores the outcome of another transaction", []() {
        TxnStatusMessage status;

        grant(LARGE_WINDOW);
        memset(&status, 0, sizeof(status));
        status.channel = CHANNEL;
        status.txn_id = TXN_ID + 1;

        TransactionSender_OnStatus(&sender, &status);

        expect(sender.state).toBe((uint8_t)kTRANSACTION_SENDER_RUNNING);
    });

    it("finishes when the agent leaves before granting", []() {
        TxnStatusMessage status;

        memset(&status, 0, sizeof(status));
        status.channel = CHANNEL;
        status.state = kTXN_STATE_ABORTED;
        status.reason = kTXN_REASON_AGENT_DISCONNECTED;

        TransactionSender_OnStatus(&sender, &status);

        expect(sender.state).toBe((uint8_t)kTRANSACTION_SENDER_FINISHED);
        expect(sender.outcome.reason).toBe((uint8_t)kTXN_REASON_AGENT_DISCONNECTED);
    });
});

static void grant(uint32_t window_bytes)
{
    TxnBeginAckMessage ack = { CHANNEL, kTXN_BEGIN_STATUS_OK, TXN_ID, window_bytes };

    TransactionSender_OnBeginAck(&sender, &ack);
}

static TxnStep classicStep(uint32_t can_id)
{
    TxnStep step;

    memset(&step, 0, sizeof(step));
    step.can_id = can_id;
    step.payload_length = FRAME_PAYLOAD_MAX_CLASSIC;
    return step;
}

static uint32_t offerUntilStopped(uint32_t count, TTRANSACTION_OFFER *stopped_by)
{
    TxnStep step;
    uint32_t accepted = 0;

    *stopped_by = kTRANSACTION_OFFER_ACCEPTED;
    while (accepted < count) {
        step = classicStep(0x100 + accepted);
        *stopped_by = TransactionSender_Offer(&sender, &step);
        if (*stopped_by != kTRANSACTION_OFFER_ACCEPTED) {
            return accepted;
        }
        accepted++;
    }
    return accepted;
}

static TxnStepsMessage pendingBatch(void)
{
    TxnStepsMessage batch;
    const uint8_t *data = NULL;
    size_t size;

    memset(&batch, 0, sizeof(batch));
    size = TransactionSender_PendingBatch(&sender, &data);
    if (size > 0) {
        TxnStepsMessage_Decode(&batch, data + MESSAGE_HEADER_SIZE, size - MESSAGE_HEADER_SIZE);
    }
    return batch;
}

static TxnStep decodedStep(const TxnStepsMessage *batch, uint8_t index)
{
    TxnStep step;
    size_t offset = 0;
    uint8_t i;

    memset(&step, 0, sizeof(step));
    for(i=0; i<=index; i++) {
        offset += TxnStep_Decode(&step, batch->records + offset, batch->records_size - offset);
    }
    return step;
}
