#include <cest>
#include <cstring>
#include <initializer_list>

extern "C" {
#include "agent/domain/transaction_runner.h"
#include "protocol/message_header.h"
}

#define RING_CAPACITY 4096
#define SMALL_RING_CAPACITY (TXN_STEP_RECORD_SIZE_MAX + 4 * CLASSIC_RECORD_SIZE)
#define CLASSIC_RECORD_SIZE 16
#define TRANSMITTED_MAX 64
#define INTERFACE_INDEX 0
#define OTHER_INTERFACE_INDEX 1
#define CHANNEL 7
#define REQUEST_ID 0x7E0
#define RESPONSE_ID 0x7E8
#define ID_MASK 0x7FF
#define SECOND_BYTE_MASK 0xFF00
#define POSITIVE_RESPONSE 0x7600
#define NEGATIVE_RESPONSE 0x7F00
#define PENDING_MASK 0xFF00FF00
#define RESPONSE_PENDING 0x78007F00
#define RESPONSE_TEMPLATE 0
#define BROADCAST_TEMPLATE 1
#define ACK_TEMPLATE 2
#define BLOCK_TEMPLATE 3
#define BLOCK_COUNTER_MASK 0xFFFF00
#define ACK_CRC_MASK 0xFFFF00
#define ACK_CRC_SHIFT 8
#define TIMEOUT_MS 50
#define TIMEOUT_US (TIMEOUT_MS * 1000)
#define GAP_US 1000
#define BATCH_ID 0x123
#define ACK_ID_BASE 0x700
#define ACK_ID_MASK 0x7E0
#define NODE_MASK 0x1F
#define BATCH_FRAMES 16
#define NODES 32
#define BATCH_CRC 0xC0DE
#define ISOTP_TEMPLATE 4
#define GENERIC_TEMPLATE 5
#define FIRST_BYTE_MASK 0xFF
#define MESSAGE_POSITIVE 0x76
#define MESSAGE_NEGATIVE 0x7F
#define MESSAGE_PENDING_MASK 0xFF00FF
#define MESSAGE_PENDING 0x78007F
#define HALF_MESSAGE 10
#define IDLE_TIMEOUT_MS 30000
#define KEEPALIVE_PERIOD_MS 2000
#define KEEPALIVE_PERIOD_US (KEEPALIVE_PERIOD_MS * 1000)

static TransactionRunner runner;
static uint8_t ring_storage[RING_CAPACITY];
static FrameMessage transmitted[TRANSMITTED_MAX];
static uint32_t transmit_count;
static bool transmit_accepts;
static TxnProgressMessage last_progress;
static uint32_t progress_count;
static TxnStatusMessage last_status;
static uint32_t status_count;
static TxnRosterReportMessage last_roster_report;
static uint32_t roster_report_count;
static uint16_t txn_id;

static void resetRunner(uint32_t ring_capacity);
static TxnBeginAckMessage beginPlain(uint64_t now_us);
static TxnBeginAckMessage beginWithNodeRoster(uint64_t now_us);
static TxnRosterMessage nodeRoster(void);
static TxnTemplateMessage responseTemplate(void);
static TxnTemplateMessage transmitOnlyTemplate(uint8_t template_id, uint32_t gap_us);
static TxnTemplateMessage batchAckTemplate(void);
static TxnTemplateMessage isotpTemplate(void);
static TxnBeginAckMessage beginWithKeepalive(uint32_t idle_timeout_ms);
static void defineTemplate(const TxnTemplateMessage *template_message);
static TxnStep requestStep(uint8_t service);
static TxnStep transmitOnlyStep(uint8_t template_id, uint32_t can_id);
static TxnStep expectOnlyStep(uint8_t template_id);
static TxnStep messageRecord(uint8_t template_id, uint8_t flags, uint8_t length);
static void pushSteps(const TxnStep *steps, uint8_t step_count, uint32_t first_seq);
static void pushLastSteps(const TxnStep *steps, uint8_t step_count, uint32_t first_seq);
static void pushStepsOfTransaction(uint16_t other_txn_id, const TxnStep *steps, uint8_t step_count, uint32_t first_seq);
static void pushBatch(uint16_t other_txn_id, const TxnStep *steps, uint8_t step_count, uint32_t first_seq, uint8_t flags);
static void answer(uint32_t can_id, uint8_t byte0, uint8_t byte1, uint8_t byte2, uint8_t byte3);
static void answerFromNode(uint8_t node, uint16_t crc);
static void answerEcho(uint32_t can_id);
static void answerBytes(std::initializer_list<uint8_t> bytes);
static bool transmitFrame(void *context, uint8_t interface_index, const FrameMessage *frame);
static void recordProgress(void *context, const TxnProgressMessage *progress);
static void recordStatus(void *context, const TxnStatusMessage *status);
static void recordRosterReport(void *context, const TxnRosterReportMessage *report);

describe("transaction_runner", []() {
    describe("begin", []() {
        beforeEach([]() {
            resetRunner(RING_CAPACITY);
        });

        it("grants a transaction with the ring window", []() {
            TxnBeginAckMessage ack;

            ack = beginPlain(0);

            expect(ack.status).toBe((uint8_t)kTXN_BEGIN_STATUS_OK);
            expect(ack.channel).toBe(CHANNEL);
            expect(ack.txn_id == 0).toBe(false);
            expect(ack.window_bytes).toBe((uint32_t)(RING_CAPACITY - TXN_STEP_RECORD_SIZE_MAX));
        });

        it("answers busy while a transaction runs", []() {
            TxnBeginAckMessage second;

            beginPlain(0);

            second = beginPlain(0);

            expect(second.status).toBe((uint8_t)kTXN_BEGIN_STATUS_BUSY);
        });
    });

    describe("request and response", []() {
        beforeEach([]() {
            TxnTemplateMessage template_message;

            resetRunner(RING_CAPACITY);
            beginPlain(0);
            template_message = responseTemplate();
            defineTemplate(&template_message);
        });

        it("transmits the request on the next tick", []() {
            TxnStep step;

            step = requestStep(0x36);
            pushSteps(&step, 1, 0);

            TransactionRunner_Tick(&runner, 100);

            expect(transmit_count).toBe((uint32_t)1);
            expect(transmitted[0].can_id).toBe((uint32_t)REQUEST_ID);
            expect(transmitted[0].payload[1]).toBe(0x36);
        });

        it("moves on with each expected answer and finishes after the last step", []() {
            TxnStep steps[2];

            steps[0] = requestStep(0x36);
            steps[1] = requestStep(0x36);
            pushLastSteps(steps, 2, 0);

            TransactionRunner_Tick(&runner, 100);
            answer(RESPONSE_ID, 0x02, 0x76, 0x01, 0x00);
            TransactionRunner_Tick(&runner, 200);
            answer(RESPONSE_ID, 0x02, 0x76, 0x02, 0x00);
            TransactionRunner_Tick(&runner, 300);

            expect(transmit_count).toBe((uint32_t)2);
            expect(status_count).toBe((uint32_t)1);
            expect(last_status.state).toBe((uint8_t)kTXN_STATE_DONE);
            expect(last_status.completed_steps).toBe((uint32_t)2);
            expect(TransactionRunner_IsActive(&runner)).toBe(false);
        });

        it("finishes when the end comes after its last step completed", []() {
            TxnStep step;
            uint32_t status_before_end;

            step = requestStep(0x36);
            pushSteps(&step, 1, 0);
            TransactionRunner_Tick(&runner, 100);
            answer(RESPONSE_ID, 0x02, 0x76, 0x00, 0x00);
            TransactionRunner_Tick(&runner, 200);
            status_before_end = status_count;

            pushLastSteps(NULL, 0, 1);
            TransactionRunner_Tick(&runner, 300);

            expect(status_before_end).toBe((uint32_t)0);
            expect(last_status.state).toBe((uint8_t)kTXN_STATE_DONE);
            expect(last_status.completed_steps).toBe((uint32_t)1);
        });

        it("aborts on steps sent after the end", []() {
            TxnStep step;

            step = requestStep(0x36);
            pushLastSteps(&step, 1, 0);
            pushSteps(&step, 1, 1);

            expect(last_status.reason).toBe((uint8_t)kTXN_REASON_INVALID_STEP);
            expect(last_status.failing_seq).toBe((uint32_t)1);
        });

        it("aborts on a negative response and reports the frame", []() {
            TxnStep step;

            step = requestStep(0x36);
            pushSteps(&step, 1, 0);
            TransactionRunner_Tick(&runner, 100);

            answer(RESPONSE_ID, 0x03, 0x7F, 0x36, 0x31);

            expect(last_status.state).toBe((uint8_t)kTXN_STATE_ABORTED);
            expect(last_status.reason).toBe((uint8_t)kTXN_REASON_ABORT_MATCH);
            expect(last_status.failing_seq).toBe((uint32_t)0);
            expect(last_status.flags).toBe((uint8_t)TXN_STATUS_FLAG_HAS_RX_FRAME);
            expect(last_status.rx_frame.payload[3]).toBe(0x31);
        });

        it("keeps waiting while the ECU answers response pending", []() {
            TxnStep step;
            uint32_t status_count_before_extended_deadline;

            step = requestStep(0x36);
            pushSteps(&step, 1, 0);
            TransactionRunner_Tick(&runner, 0);
            answer(RESPONSE_ID, 0x03, 0x7F, 0x36, 0x78);
            TransactionRunner_Tick(&runner, 40000);

            TransactionRunner_Tick(&runner, TIMEOUT_US + 1);
            status_count_before_extended_deadline = status_count;
            TransactionRunner_Tick(&runner, 40000 + TIMEOUT_US);

            expect(status_count_before_extended_deadline).toBe((uint32_t)0);
            expect(last_status.reason).toBe((uint8_t)kTXN_REASON_TIMEOUT);
        });

        it("aborts when the answer does not come in time", []() {
            TxnStep step;

            step = requestStep(0x36);
            pushSteps(&step, 1, 0);
            TransactionRunner_Tick(&runner, 0);

            TransactionRunner_Tick(&runner, TIMEOUT_US);

            expect(last_status.state).toBe((uint8_t)kTXN_STATE_ABORTED);
            expect(last_status.reason).toBe((uint8_t)kTXN_REASON_TIMEOUT);
        });

        it("rejects an unexpected value when the step expects its own", []() {
            TxnTemplateMessage block_template;
            TxnStep step;

            block_template = responseTemplate();
            block_template.template_id = BLOCK_TEMPLATE;
            block_template.patterns[kTXN_PATTERN_ADVANCE].payload_mask = BLOCK_COUNTER_MASK;
            defineTemplate(&block_template);
            step = requestStep(0x36);
            step.template_id = BLOCK_TEMPLATE;
            step.flags = TXN_STEP_FLAG_EXPECT_VALUE;
            step.expect_value = 0x057600;
            pushSteps(&step, 1, 0);
            TransactionRunner_Tick(&runner, 0);

            answer(RESPONSE_ID, 0x02, 0x76, 0x04, 0x00);

            expect(last_status.reason).toBe((uint8_t)kTXN_REASON_VALUE_MISMATCH);
            expect(last_status.expected).toBe((uint64_t)0x057600);
            expect(last_status.got).toBe((uint64_t)0x047602);
        });

        it("does not match the echo of its own request", []() {
            TxnStep step;

            step = requestStep(0x36);
            pushSteps(&step, 1, 0);
            TransactionRunner_Tick(&runner, 0);

            answerEcho(RESPONSE_ID);
            TransactionRunner_Tick(&runner, 100);

            expect(runner.completed_steps).toBe((uint32_t)0);
        });

        it("ignores answers on another interface", []() {
            FrameMessage frame = { 0 };
            TxnStep step;

            step = requestStep(0x36);
            pushSteps(&step, 1, 0);
            TransactionRunner_Tick(&runner, 0);
            frame.can_id = RESPONSE_ID;
            frame.payload_length = 2;
            frame.payload[1] = 0x76;

            TransactionRunner_OnFrame(&runner, OTHER_INTERFACE_INDEX, &frame);
            TransactionRunner_Tick(&runner, 100);

            expect(runner.completed_steps).toBe((uint32_t)0);
        });

        it("cancels on request", []() {
            TxnStep step;

            step = requestStep(0x36);
            pushSteps(&step, 1, 0);
            TransactionRunner_Tick(&runner, 0);

            TransactionRunner_Abort(&runner, txn_id);

            expect(last_status.reason).toBe((uint8_t)kTXN_REASON_CANCELLED);
            expect(TransactionRunner_IsActive(&runner)).toBe(false);
        });
    });

    describe("templates", []() {
        beforeEach([]() {
            resetRunner(RING_CAPACITY);
            beginPlain(0);
        });

        it("completes a step on timeout when the template is soft", []() {
            TxnTemplateMessage template_message;
            TxnStep step;

            template_message = responseTemplate();
            template_message.flags = TXN_TEMPLATE_FLAG_SOFT;
            defineTemplate(&template_message);
            step = requestStep(0x3E);
            pushLastSteps(&step, 1, 0);
            TransactionRunner_Tick(&runner, 0);

            TransactionRunner_Tick(&runner, TIMEOUT_US);

            expect(last_status.state).toBe((uint8_t)kTXN_STATE_DONE);
        });

        it("ignores an unexpected value without mismatch abort", []() {
            TxnTemplateMessage template_message;
            TxnStep step;

            template_message = responseTemplate();
            template_message.flags = 0;
            defineTemplate(&template_message);
            step = requestStep(0x36);
            pushLastSteps(&step, 1, 0);
            TransactionRunner_Tick(&runner, 0);

            answer(RESPONSE_ID, 0x02, 0x75, 0x00, 0x00);
            TransactionRunner_Tick(&runner, 100);
            answer(RESPONSE_ID, 0x02, 0x76, 0x00, 0x00);
            TransactionRunner_Tick(&runner, 200);

            expect(last_status.state).toBe((uint8_t)kTXN_STATE_DONE);
        });

        it("waits for count answers", []() {
            TxnTemplateMessage template_message;
            TxnStep step;
            uint32_t completed_after_two;

            template_message = responseTemplate();
            template_message.count_k = 3;
            defineTemplate(&template_message);
            step = requestStep(0x22);
            pushSteps(&step, 1, 0);
            TransactionRunner_Tick(&runner, 0);

            answer(RESPONSE_ID, 0x02, 0x76, 0x00, 0x00);
            answer(RESPONSE_ID, 0x02, 0x76, 0x00, 0x00);
            TransactionRunner_Tick(&runner, 100);
            completed_after_two = runner.completed_steps;
            answer(RESPONSE_ID, 0x02, 0x76, 0x00, 0x00);
            TransactionRunner_Tick(&runner, 200);

            expect(completed_after_two).toBe((uint32_t)0);
            expect(runner.completed_steps).toBe((uint32_t)1);
        });

        it("aborts when a template is defined twice", []() {
            TxnTemplateMessage template_message;

            template_message = responseTemplate();
            defineTemplate(&template_message);

            defineTemplate(&template_message);

            expect(last_status.reason).toBe((uint8_t)kTXN_REASON_INVALID_STEP);
        });

        it("aborts when a step names an undefined template", []() {
            TxnStep step;

            step = requestStep(0x36);

            pushSteps(&step, 1, 0);

            expect(last_status.reason).toBe((uint8_t)kTXN_REASON_INVALID_STEP);
            expect(last_status.failing_seq).toBe((uint32_t)0);
        });
    });

    describe("roster", []() {
        beforeEach([]() {
            TxnTemplateMessage template_message;
            TxnStep step;

            resetRunner(RING_CAPACITY);
            beginWithNodeRoster(0);
            template_message = batchAckTemplate();
            defineTemplate(&template_message);
            step = expectOnlyStep(ACK_TEMPLATE);
            step.flags |= TXN_STEP_FLAG_EXPECT_VALUE;
            step.expect_value = (uint64_t)BATCH_CRC << ACK_CRC_SHIFT;
            pushLastSteps(&step, 1, 0);
            TransactionRunner_Tick(&runner, 0);
        });

        it("counts each node once and reports who did not answer", []() {
            answerFromNode(0, BATCH_CRC);
            answerFromNode(0, BATCH_CRC);
            answerFromNode(5, BATCH_CRC);

            TransactionRunner_Tick(&runner, TIMEOUT_US);

            expect(last_status.reason).toBe((uint8_t)kTXN_REASON_TIMEOUT);
            expect(last_status.seen_bitmap).toBe((uint64_t)((1u << 0) | (1u << 5)));
            expect(last_status.flags & TXN_STATUS_FLAG_ROSTER_STEP).toBe((uint8_t)TXN_STATUS_FLAG_ROSTER_STEP);
            expect(roster_report_count).toBe((uint32_t)0);
        });

        it("aborts on a node acknowledging another CRC and names it", []() {
            answerFromNode(9, BATCH_CRC + 1);

            expect(last_status.reason).toBe((uint8_t)kTXN_REASON_VALUE_MISMATCH);
            expect(last_status.offending_origin).toBe((uint32_t)9);
        });

        it("finishes once every node answered, with nothing to report", []() {
            uint8_t node;

            for(node=0; node<NODES; node++) {
                answerFromNode(node, BATCH_CRC);
            }
            TransactionRunner_Tick(&runner, 100);

            expect(last_status.state).toBe((uint8_t)kTXN_STATE_DONE);
            expect(roster_report_count).toBe((uint32_t)0);
        });
    });

    describe("roster definition", []() {
        beforeEach([]() {
            resetRunner(RING_CAPACITY);
            beginPlain(0);
        });

        it("aborts on a second roster", []() {
            TxnRosterMessage roster;

            roster = nodeRoster();
            TransactionRunner_DefineRoster(&runner, &roster);

            TransactionRunner_DefineRoster(&runner, &roster);

            expect(last_status.reason).toBe((uint8_t)kTXN_REASON_INVALID_STEP);
        });

        it("aborts on a roster without a way to tell the origin", []() {
            TxnRosterMessage roster;

            roster = nodeRoster();
            roster.origin.source = kTXN_ORIGIN_SOURCE_NONE;

            TransactionRunner_DefineRoster(&runner, &roster);

            expect(last_status.reason).toBe((uint8_t)kTXN_REASON_INVALID_STEP);
        });

        it("ignores a roster of another transaction", []() {
            TxnRosterMessage roster;

            roster = nodeRoster();
            roster.txn_id = (uint16_t)(txn_id + 1);

            TransactionRunner_DefineRoster(&runner, &roster);

            expect(status_count).toBe((uint32_t)0);
            expect(runner.roster_count).toBe(0);
        });

        it("aborts on a template waiting for the roster before it is defined", []() {
            TxnTemplateMessage template_message;

            template_message = batchAckTemplate();

            defineTemplate(&template_message);

            expect(last_status.reason).toBe((uint8_t)kTXN_REASON_INVALID_STEP);
        });
    });

    describe("roster reports", []() {
        beforeEach([]() {
            TxnTemplateMessage template_message;
            TxnStep steps[2];

            resetRunner(RING_CAPACITY);
            beginWithNodeRoster(0);
            template_message = batchAckTemplate();
            template_message.flags = TXN_TEMPLATE_FLAG_SOFT;
            template_message.patterns[kTXN_PATTERN_ADVANCE].payload_mask = 0;
            defineTemplate(&template_message);
            steps[0] = expectOnlyStep(ACK_TEMPLATE);
            steps[1] = expectOnlyStep(ACK_TEMPLATE);
            pushLastSteps(steps, 2, 0);
            TransactionRunner_Tick(&runner, 0);
        });

        it("reports who had not answered when a soft step moves on", []() {
            answerFromNode(3, 0);
            answerFromNode(7, 0);

            TransactionRunner_Tick(&runner, TIMEOUT_US);

            expect(roster_report_count).toBe((uint32_t)1);
            expect(last_roster_report.channel).toBe(CHANNEL);
            expect(last_roster_report.txn_id).toBe(txn_id);
            expect(last_roster_report.seq).toBe((uint32_t)0);
            expect(last_roster_report.seen_bitmap).toBe((uint64_t)((1u << 3) | (1u << 7)));
            expect(runner.completed_steps).toBe((uint32_t)1);
            expect(status_count).toBe((uint32_t)0);
        });

        it("reports every step that moves on without every node", []() {
            TransactionRunner_Tick(&runner, TIMEOUT_US);
            answerFromNode(1, 0);

            TransactionRunner_Tick(&runner, 2 * TIMEOUT_US);

            expect(roster_report_count).toBe((uint32_t)2);
            expect(last_roster_report.seq).toBe((uint32_t)1);
            expect(last_roster_report.seen_bitmap).toBe((uint64_t)(1u << 1));
            expect(last_status.state).toBe((uint8_t)kTXN_STATE_DONE);
        });
    });

    describe("roster quorum", []() {
        beforeEach([]() {
            TxnTemplateMessage template_message;
            TxnStep step;

            resetRunner(RING_CAPACITY);
            beginWithNodeRoster(0);
            template_message = batchAckTemplate();
            template_message.advance_mode = kTXN_ADVANCE_MODE_ROSTER_K;
            template_message.count_k = 2;
            template_message.patterns[kTXN_PATTERN_ADVANCE].payload_mask = 0;
            defineTemplate(&template_message);
            step = expectOnlyStep(ACK_TEMPLATE);
            pushLastSteps(&step, 1, 0);
            TransactionRunner_Tick(&runner, 0);
        });

        it("reports the nodes still missing when the quorum completes the step", []() {
            answerFromNode(4, 0);
            answerFromNode(6, 0);

            TransactionRunner_Tick(&runner, 100);

            expect(roster_report_count).toBe((uint32_t)1);
            expect(last_roster_report.seen_bitmap).toBe((uint64_t)((1u << 4) | (1u << 6)));
            expect(last_status.state).toBe((uint8_t)kTXN_STATE_DONE);
        });
    });

    describe("iso-tp steps", []() {
        beforeEach([]() {
            TxnTemplateMessage template_message;

            resetRunner(RING_CAPACITY);
            beginPlain(0);
            template_message = isotpTemplate();
            defineTemplate(&template_message);
        });

        it("sends a message gathered from its records as one ISO-TP transfer", []() {
            TxnStep records[2];

            records[0] = messageRecord(ISOTP_TEMPLATE, TXN_STEP_FLAG_MESSAGE | TXN_STEP_FLAG_CONTINUED, HALF_MESSAGE);
            records[1] = messageRecord(ISOTP_TEMPLATE, TXN_STEP_FLAG_MESSAGE, HALF_MESSAGE);
            pushSteps(records, 2, 0);

            TransactionRunner_Tick(&runner, 100);
            answerBytes({ 0x30, 0x00, 0x00 });
            TransactionRunner_Tick(&runner, 200);

            expect(transmit_count).toBe((uint32_t)3);
            expect(transmitted[0].can_id).toBe((uint32_t)REQUEST_ID);
            expect(transmitted[0].payload[0]).toBe(0x10);
            expect(transmitted[0].payload[1]).toBe(2 * HALF_MESSAGE);
            expect(transmitted[2].payload[0]).toBe(0x22);
        });

        it("times the answer from the end of the transfer", []() {
            TxnStep records[2];

            records[0] = messageRecord(ISOTP_TEMPLATE, TXN_STEP_FLAG_MESSAGE | TXN_STEP_FLAG_CONTINUED, HALF_MESSAGE);
            records[1] = messageRecord(ISOTP_TEMPLATE, TXN_STEP_FLAG_MESSAGE, HALF_MESSAGE);
            pushLastSteps(records, 2, 0);

            TransactionRunner_Tick(&runner, 100);
            answerBytes({ 0x30, 0x00, 0x00 });
            TransactionRunner_Tick(&runner, 40100);
            TransactionRunner_Tick(&runner, 80100);
            answerBytes({ 0x02, MESSAGE_POSITIVE, 0x01 });
            TransactionRunner_Tick(&runner, 80200);

            expect(last_status.state).toBe((uint8_t)kTXN_STATE_DONE);
            expect(last_status.completed_steps).toBe((uint32_t)1);
        });

        it("keeps waiting after a response pending message", []() {
            TxnStep request;
            bool active_after_first_timeout;

            request = messageRecord(ISOTP_TEMPLATE, TXN_STEP_FLAG_MESSAGE, 3);
            pushLastSteps(&request, 1, 0);

            TransactionRunner_Tick(&runner, 100);
            answerBytes({ 0x03, MESSAGE_NEGATIVE, 0x36, 0x78 });
            TransactionRunner_Tick(&runner, 40100);
            TransactionRunner_Tick(&runner, 60100);
            active_after_first_timeout = TransactionRunner_IsActive(&runner);
            answerBytes({ 0x02, MESSAGE_POSITIVE, 0x01 });
            TransactionRunner_Tick(&runner, 60200);

            expect(active_after_first_timeout).toBe(true);
            expect(last_status.state).toBe((uint8_t)kTXN_STATE_DONE);
        });

        it("waits the extended time after a response pending", []() {
            TxnTemplateMessage template_message;
            TxnStep request;
            bool active_before_extended_timeout;

            resetRunner(RING_CAPACITY);
            beginPlain(0);
            template_message = isotpTemplate();
            template_message.extend_timeout_ms = 5 * TIMEOUT_MS;
            defineTemplate(&template_message);
            request = messageRecord(ISOTP_TEMPLATE, TXN_STEP_FLAG_MESSAGE, 3);
            pushLastSteps(&request, 1, 0);

            TransactionRunner_Tick(&runner, 100);
            answerBytes({ 0x03, MESSAGE_NEGATIVE, 0x36, 0x78 });
            TransactionRunner_Tick(&runner, 200);
            TransactionRunner_Tick(&runner, 200 + 4 * TIMEOUT_US);
            active_before_extended_timeout = TransactionRunner_IsActive(&runner);
            TransactionRunner_Tick(&runner, 200 + 5 * TIMEOUT_US);

            expect(active_before_extended_timeout).toBe(true);
            expect(last_status.reason).toBe((uint8_t)kTXN_REASON_TIMEOUT);
        });

        it("reassembles a multi-frame answer, answering it with flow control", []() {
            TxnStep request;

            request = messageRecord(ISOTP_TEMPLATE, TXN_STEP_FLAG_MESSAGE, 3);
            pushLastSteps(&request, 1, 0);

            TransactionRunner_Tick(&runner, 100);
            answerBytes({ 0x10, 0x0A, MESSAGE_POSITIVE, 0x01, 0x02, 0x03, 0x04, 0x05 });
            TransactionRunner_Tick(&runner, 200);
            answerBytes({ 0x21, 0x06, 0x07, 0x08, 0x09 });
            TransactionRunner_Tick(&runner, 300);

            expect(transmit_count).toBe((uint32_t)2);
            expect(transmitted[1].can_id).toBe((uint32_t)REQUEST_ID);
            expect(transmitted[1].payload[0]).toBe(0x30);
            expect(last_status.state).toBe((uint8_t)kTXN_STATE_DONE);
        });

        it("aborts with the ISO-TP reason when the flow control never comes", []() {
            TxnStep records[2];

            records[0] = messageRecord(ISOTP_TEMPLATE, TXN_STEP_FLAG_MESSAGE | TXN_STEP_FLAG_CONTINUED, HALF_MESSAGE);
            records[1] = messageRecord(ISOTP_TEMPLATE, TXN_STEP_FLAG_MESSAGE, HALF_MESSAGE);
            pushSteps(records, 2, 0);

            TransactionRunner_Tick(&runner, 100);
            TransactionRunner_Tick(&runner, 100 + ISOTP_TIMEOUT_US);

            expect(last_status.state).toBe((uint8_t)kTXN_STATE_ABORTED);
            expect(last_status.reason).toBe((uint8_t)kTXN_REASON_FLOW_CONTROL_TIMEOUT);
        });

        it("counts a message spread over records as one step", []() {
            TxnStep first_message[2];
            TxnStep second_message;

            first_message[0] = messageRecord(ISOTP_TEMPLATE, TXN_STEP_FLAG_MESSAGE | TXN_STEP_FLAG_CONTINUED, 2);
            first_message[1] = messageRecord(ISOTP_TEMPLATE, TXN_STEP_FLAG_MESSAGE, 1);
            second_message = messageRecord(ISOTP_TEMPLATE, TXN_STEP_FLAG_MESSAGE, 3);
            pushSteps(first_message, 2, 0);
            pushLastSteps(&second_message, 1, 1);

            TransactionRunner_Tick(&runner, 100);
            answerBytes({ 0x02, MESSAGE_POSITIVE, 0x01 });
            TransactionRunner_Tick(&runner, 200);
            answerBytes({ 0x02, MESSAGE_POSITIVE, 0x02 });
            TransactionRunner_Tick(&runner, 300);

            expect(last_status.state).toBe((uint8_t)kTXN_STATE_DONE);
            expect(last_status.completed_steps).toBe((uint32_t)2);
        });

        it("aborts on a message record naming a template that sends frames", []() {
            TxnTemplateMessage generic;
            TxnStep record;

            generic = transmitOnlyTemplate(GENERIC_TEMPLATE, 0);
            defineTemplate(&generic);
            record = messageRecord(GENERIC_TEMPLATE, TXN_STEP_FLAG_MESSAGE, 3);

            pushSteps(&record, 1, 0);

            expect(last_status.reason).toBe((uint8_t)kTXN_REASON_INVALID_STEP);
        });
    });

    describe("keepalive", []() {
        beforeEach([]() {
            TxnTemplateMessage template_message;

            resetRunner(RING_CAPACITY);
            beginWithKeepalive(IDLE_TIMEOUT_MS);
            template_message = responseTemplate();
            defineTemplate(&template_message);
        });

        it("sends the keepalive frame every period while it waits for steps", []() {
            uint32_t sent_before_period;

            TransactionRunner_Tick(&runner, KEEPALIVE_PERIOD_US - 1);
            sent_before_period = transmit_count;
            TransactionRunner_Tick(&runner, KEEPALIVE_PERIOD_US);
            TransactionRunner_Tick(&runner, 2 * KEEPALIVE_PERIOD_US);

            expect(sent_before_period).toBe((uint32_t)0);
            expect(transmit_count).toBe((uint32_t)2);
            expect(transmitted[0].can_id).toBe((uint32_t)REQUEST_ID);
            expect(transmitted[0].payload[1]).toBe(0x3E);
        });

        it("stays quiet while a step waits for its answer", []() {
            TxnTemplateMessage slow_answer;
            TxnStep step;

            slow_answer = responseTemplate();
            slow_answer.template_id = BLOCK_TEMPLATE;
            slow_answer.timeout_ms = 3 * KEEPALIVE_PERIOD_MS;
            defineTemplate(&slow_answer);
            step = requestStep(0x36);
            step.template_id = BLOCK_TEMPLATE;
            pushSteps(&step, 1, 0);

            TransactionRunner_Tick(&runner, 100);
            TransactionRunner_Tick(&runner, 100 + KEEPALIVE_PERIOD_US);

            expect(TransactionRunner_IsActive(&runner)).toBe(true);
            expect(transmit_count).toBe((uint32_t)1);
            expect(transmitted[0].payload[1]).toBe(0x36);
        });

        it("waits for steps as long as the transaction asked", []() {
            TransactionRunner_Tick(&runner, TRANSACTION_STARVATION_TIMEOUT_US);
            TransactionRunner_Tick(&runner, (uint64_t)IDLE_TIMEOUT_MS * 1000 - 1);

            expect(status_count).toBe((uint32_t)0);
            expect(TransactionRunner_IsActive(&runner)).toBe(true);
        });

        it("starves once the idle timeout passes", []() {
            TransactionRunner_Tick(&runner, 1);
            TransactionRunner_Tick(&runner, (uint64_t)IDLE_TIMEOUT_MS * 1000 + 1);

            expect(last_status.reason).toBe((uint8_t)kTXN_REASON_STARVED);
        });
    });

    describe("transmit", []() {
        beforeEach([]() {
            resetRunner(RING_CAPACITY);
            beginPlain(0);
        });

        it("runs steps that do not wait back to back", []() {
            TxnTemplateMessage template_message;
            TxnStep steps[3];

            template_message = transmitOnlyTemplate(BROADCAST_TEMPLATE, 0);
            defineTemplate(&template_message);
            steps[0] = transmitOnlyStep(BROADCAST_TEMPLATE, 0x100);
            steps[1] = transmitOnlyStep(BROADCAST_TEMPLATE, 0x101);
            steps[2] = transmitOnlyStep(BROADCAST_TEMPLATE, 0x102);
            pushSteps(steps, 3, 0);

            TransactionRunner_Tick(&runner, 0);

            expect(transmit_count).toBe((uint32_t)3);
            expect(transmitted[2].can_id).toBe((uint32_t)0x102);
        });

        it("holds the next frame until the gap has passed", []() {
            TxnTemplateMessage template_message;
            TxnStep steps[2];
            uint32_t sent_before_gap;
            uint64_t wake_us;

            template_message = transmitOnlyTemplate(BROADCAST_TEMPLATE, GAP_US);
            defineTemplate(&template_message);
            steps[0] = transmitOnlyStep(BROADCAST_TEMPLATE, 0x100);
            steps[1] = transmitOnlyStep(BROADCAST_TEMPLATE, 0x101);
            pushSteps(steps, 2, 0);

            TransactionRunner_Tick(&runner, 10);
            wake_us = TransactionRunner_NextWakeUs(&runner);
            TransactionRunner_Tick(&runner, 10 + GAP_US - 1);
            sent_before_gap = transmit_count;
            TransactionRunner_Tick(&runner, 10 + GAP_US);

            expect(wake_us).toBe((uint64_t)(10 + GAP_US));
            expect(sent_before_gap).toBe((uint32_t)1);
            expect(transmit_count).toBe((uint32_t)2);
        });

        it("retries a refused frame until the step times out", []() {
            TxnTemplateMessage template_message;
            TxnStep step;
            uint32_t attempts_before_timeout;

            template_message = transmitOnlyTemplate(BROADCAST_TEMPLATE, 0);
            template_message.timeout_ms = TIMEOUT_MS;
            defineTemplate(&template_message);
            step = transmitOnlyStep(BROADCAST_TEMPLATE, 0x100);
            pushSteps(&step, 1, 0);
            transmit_accepts = false;

            TransactionRunner_Tick(&runner, 0);
            TransactionRunner_Tick(&runner, TRANSACTION_TX_RETRY_US);
            attempts_before_timeout = transmit_count;
            TransactionRunner_Tick(&runner, TIMEOUT_US);

            expect(attempts_before_timeout).toBe((uint32_t)2);
            expect(last_status.reason).toBe((uint8_t)kTXN_REASON_TX_FAILED);
        });

        it("waits on an expect-only step without transmitting", []() {
            TxnTemplateMessage template_message;
            TxnStep step;

            template_message = responseTemplate();
            defineTemplate(&template_message);
            step = expectOnlyStep(RESPONSE_TEMPLATE);
            pushSteps(&step, 1, 0);

            TransactionRunner_Tick(&runner, 0);

            expect(transmit_count).toBe((uint32_t)0);
            expect(runner.current.phase).toBe((uint8_t)kTRANSACTION_PHASE_WAIT);
        });

        it("takes an answer that outran its expect-only step", []() {
            TxnTemplateMessage template_message;
            TxnStep frame_step;
            TxnStep listening_step;

            template_message = responseTemplate();
            defineTemplate(&template_message);
            template_message = transmitOnlyTemplate(BROADCAST_TEMPLATE, 0);
            defineTemplate(&template_message);
            frame_step = transmitOnlyStep(BROADCAST_TEMPLATE, REQUEST_ID);
            listening_step = expectOnlyStep(RESPONSE_TEMPLATE);

            pushSteps(&frame_step, 1, 0);
            TransactionRunner_Tick(&runner, 100);
            answer(RESPONSE_ID, 0x02, 0x76, 0x00, 0x00);
            pushLastSteps(&listening_step, 1, 1);
            TransactionRunner_Tick(&runner, 200);

            expect(last_status.state).toBe((uint8_t)kTXN_STATE_DONE);
            expect(last_status.completed_steps).toBe((uint32_t)2);
        });

        it("does not let a step that transmits take an answer from before its frame", []() {
            TxnTemplateMessage template_message;
            TxnStep frame_step;
            TxnStep request;

            template_message = responseTemplate();
            defineTemplate(&template_message);
            template_message = transmitOnlyTemplate(BROADCAST_TEMPLATE, 0);
            defineTemplate(&template_message);
            frame_step = transmitOnlyStep(BROADCAST_TEMPLATE, REQUEST_ID);
            request = requestStep(0x36);

            pushSteps(&frame_step, 1, 0);
            TransactionRunner_Tick(&runner, 100);
            answer(RESPONSE_ID, 0x02, 0x76, 0x00, 0x00);
            pushLastSteps(&request, 1, 1);
            TransactionRunner_Tick(&runner, 200);
            TransactionRunner_Tick(&runner, 200 + TIMEOUT_US);

            expect(last_status.reason).toBe((uint8_t)kTXN_REASON_TIMEOUT);
        });
    });

    describe("flow", []() {
        beforeEach([]() {
            TxnTemplateMessage template_message;

            resetRunner(SMALL_RING_CAPACITY);
            beginPlain(0);
            template_message = transmitOnlyTemplate(BROADCAST_TEMPLATE, 0);
            defineTemplate(&template_message);
        });

        it("reports progress once a quarter of the window is consumed", []() {
            TxnStep steps[2];

            steps[0] = transmitOnlyStep(BROADCAST_TEMPLATE, 0x100);
            steps[1] = transmitOnlyStep(BROADCAST_TEMPLATE, 0x101);
            pushSteps(steps, 1, 0);

            TransactionRunner_Tick(&runner, 0);

            expect(progress_count).toBe((uint32_t)1);
            expect(last_progress.completed_steps).toBe((uint32_t)1);
            expect(last_progress.consumed_bytes).toBe((uint32_t)CLASSIC_RECORD_SIZE);
        });

        it("aborts with overrun when a batch exceeds the window", []() {
            TxnStep steps[5];
            uint8_t i;

            for(i=0; i<5; i++) {
                steps[i] = transmitOnlyStep(BROADCAST_TEMPLATE, 0x100 + i);
            }

            pushSteps(steps, 5, 0);

            expect(last_status.reason).toBe((uint8_t)kTXN_REASON_OVERRUN);
            expect(last_status.failing_seq).toBe((uint32_t)4);
        });

        it("accepts a new batch once the credit came back", []() {
            TxnStep steps[4];
            uint8_t i;

            for(i=0; i<4; i++) {
                steps[i] = transmitOnlyStep(BROADCAST_TEMPLATE, 0x100 + i);
            }
            pushSteps(steps, 4, 0);
            TransactionRunner_Tick(&runner, 0);

            pushSteps(steps, 4, 4);
            TransactionRunner_Tick(&runner, 100);

            expect(status_count).toBe((uint32_t)0);
            expect(transmit_count).toBe((uint32_t)8);
            expect(transmitted[7].can_id).toBe((uint32_t)0x103);
        });

        it("aborts when a batch skips a sequence number", []() {
            TxnStep step;

            step = transmitOnlyStep(BROADCAST_TEMPLATE, 0x100);

            pushSteps(&step, 1, 1);

            expect(last_status.reason).toBe((uint8_t)kTXN_REASON_INVALID_STEP);
            expect(last_status.failing_seq).toBe((uint32_t)1);
        });

        it("ignores steps of another transaction", []() {
            TxnStep step;

            step = transmitOnlyStep(BROADCAST_TEMPLATE, 0x100);

            pushStepsOfTransaction((uint16_t)(txn_id + 1), &step, 1, 0);
            TransactionRunner_Tick(&runner, 0);

            expect(transmit_count).toBe((uint32_t)0);
            expect(status_count).toBe((uint32_t)0);
        });

        it("aborts as starved when no step arrives in time", []() {
            TransactionRunner_Tick(&runner, TRANSACTION_STARVATION_TIMEOUT_US);

            expect(last_status.reason).toBe((uint8_t)kTXN_REASON_STARVED);
        });
    });

    describe("reference sequence", []() {
        beforeEach([]() {
            resetRunner(RING_CAPACITY);
        });

        it("writes a 16-frame batch and waits for 32 nodes to acknowledge its CRC", []() {
            TxnTemplateMessage write_template;
            TxnTemplateMessage ack_template;
            TxnStep steps[BATCH_FRAMES + 1];
            uint8_t node;
            uint8_t i;

            beginWithNodeRoster(0);
            write_template = transmitOnlyTemplate(BROADCAST_TEMPLATE, 0);
            ack_template = batchAckTemplate();
            defineTemplate(&write_template);
            defineTemplate(&ack_template);
            for(i=0; i<BATCH_FRAMES; i++) {
                steps[i] = transmitOnlyStep(BROADCAST_TEMPLATE, BATCH_ID);
            }
            steps[BATCH_FRAMES] = expectOnlyStep(ACK_TEMPLATE);
            steps[BATCH_FRAMES].flags |= TXN_STEP_FLAG_EXPECT_VALUE;
            steps[BATCH_FRAMES].expect_value = (uint64_t)BATCH_CRC << ACK_CRC_SHIFT;
            pushSteps(steps, BATCH_FRAMES / 2, 0);
            pushLastSteps(steps + BATCH_FRAMES / 2, BATCH_FRAMES / 2 + 1, BATCH_FRAMES / 2);

            TransactionRunner_Tick(&runner, 0);
            for(node=0; node<NODES; node++) {
                answerFromNode(node, BATCH_CRC);
            }
            TransactionRunner_Tick(&runner, 1000);

            expect(transmit_count).toBe((uint32_t)BATCH_FRAMES);
            expect(last_status.state).toBe((uint8_t)kTXN_STATE_DONE);
            expect(last_status.completed_steps).toBe((uint32_t)(BATCH_FRAMES + 1));
        });
    });
});

static void resetRunner(uint32_t ring_capacity)
{
    TransactionRunnerPort port = { NULL, transmitFrame, recordProgress, recordStatus, recordRosterReport };

    memset(transmitted, 0, sizeof(transmitted));
    transmit_count = 0;
    transmit_accepts = true;
    progress_count = 0;
    status_count = 0;
    roster_report_count = 0;
    memset(&last_roster_report, 0, sizeof(last_roster_report));
    memset(&last_progress, 0, sizeof(last_progress));
    memset(&last_status, 0, sizeof(last_status));
    TransactionRunner_Init(&runner, port, ring_storage, ring_capacity);
}

static TxnBeginAckMessage beginPlain(uint64_t now_us)
{
    TxnBeginMessage begin = { 0 };
    TxnBeginAckMessage ack;

    begin.channel = CHANNEL;
    TransactionRunner_Begin(&runner, &begin, INTERFACE_INDEX, now_us, &ack);
    txn_id = ack.txn_id;
    return ack;
}

static TxnBeginAckMessage beginWithNodeRoster(uint64_t now_us)
{
    TxnBeginAckMessage ack;
    TxnRosterMessage roster;

    ack = beginPlain(now_us);
    roster = nodeRoster();
    TransactionRunner_DefineRoster(&runner, &roster);
    return ack;
}

static TxnRosterMessage nodeRoster(void)
{
    TxnRosterMessage roster = { 0 };
    uint8_t node;

    roster.channel = CHANNEL;
    roster.txn_id = txn_id;
    roster.origin.source = kTXN_ORIGIN_SOURCE_CAN_ID;
    roster.origin.mask = NODE_MASK;
    roster.roster_count = NODES;
    for(node=0; node<NODES; node++) {
        roster.roster[node] = node;
    }
    return roster;
}

static TxnTemplateMessage responseTemplate(void)
{
    TxnTemplateMessage template_message = { 0 };

    template_message.channel = CHANNEL;
    template_message.template_id = RESPONSE_TEMPLATE;
    template_message.txn_id = txn_id;
    template_message.advance_mode = kTXN_ADVANCE_MODE_COUNT;
    template_message.flags = TXN_TEMPLATE_FLAG_MISMATCH_ABORT;
    template_message.count_k = 1;
    template_message.patterns_present = (1u << kTXN_PATTERN_ADVANCE) | (1u << kTXN_PATTERN_ABORT) | (1u << kTXN_PATTERN_EXTEND);
    template_message.timeout_ms = TIMEOUT_MS;
    template_message.patterns[kTXN_PATTERN_ADVANCE] = { RESPONSE_ID, ID_MASK, SECOND_BYTE_MASK, POSITIVE_RESPONSE };
    template_message.patterns[kTXN_PATTERN_ABORT] = { RESPONSE_ID, ID_MASK, SECOND_BYTE_MASK, NEGATIVE_RESPONSE };
    template_message.patterns[kTXN_PATTERN_EXTEND] = { RESPONSE_ID, ID_MASK, PENDING_MASK, RESPONSE_PENDING };
    return template_message;
}

static TxnTemplateMessage transmitOnlyTemplate(uint8_t template_id, uint32_t gap_us)
{
    TxnTemplateMessage template_message = { 0 };

    template_message.channel = CHANNEL;
    template_message.template_id = template_id;
    template_message.txn_id = txn_id;
    template_message.advance_mode = kTXN_ADVANCE_MODE_NONE;
    template_message.gap_us = gap_us;
    return template_message;
}

static TxnTemplateMessage batchAckTemplate(void)
{
    TxnTemplateMessage template_message = { 0 };

    template_message.channel = CHANNEL;
    template_message.template_id = ACK_TEMPLATE;
    template_message.txn_id = txn_id;
    template_message.advance_mode = kTXN_ADVANCE_MODE_ROSTER_ALL;
    template_message.flags = TXN_TEMPLATE_FLAG_MISMATCH_ABORT;
    template_message.patterns_present = 1u << kTXN_PATTERN_ADVANCE;
    template_message.timeout_ms = TIMEOUT_MS;
    template_message.patterns[kTXN_PATTERN_ADVANCE] = { ACK_ID_BASE, ACK_ID_MASK, ACK_CRC_MASK, 0 };
    return template_message;
}

static TxnTemplateMessage isotpTemplate(void)
{
    TxnTemplateMessage template_message = { 0 };

    template_message.channel = CHANNEL;
    template_message.template_id = ISOTP_TEMPLATE;
    template_message.txn_id = txn_id;
    template_message.type = kTXN_TEMPLATE_TYPE_ISOTP;
    template_message.advance_mode = kTXN_ADVANCE_MODE_COUNT;
    template_message.count_k = 1;
    template_message.patterns_present = (1u << kTXN_PATTERN_ADVANCE) | (1u << kTXN_PATTERN_ABORT) | (1u << kTXN_PATTERN_EXTEND);
    template_message.timeout_ms = TIMEOUT_MS;
    template_message.patterns[kTXN_PATTERN_ADVANCE] = { RESPONSE_ID, ID_MASK, FIRST_BYTE_MASK, MESSAGE_POSITIVE };
    template_message.patterns[kTXN_PATTERN_ABORT] = { RESPONSE_ID, ID_MASK, FIRST_BYTE_MASK, MESSAGE_NEGATIVE };
    template_message.patterns[kTXN_PATTERN_EXTEND] = { RESPONSE_ID, ID_MASK, MESSAGE_PENDING_MASK, MESSAGE_PENDING };
    template_message.isotp.response_id = RESPONSE_ID;
    template_message.isotp.tx_data_length = FRAME_PAYLOAD_MAX_CLASSIC;
    return template_message;
}

static TxnBeginAckMessage beginWithKeepalive(uint32_t idle_timeout_ms)
{
    TxnBeginMessage begin = { 0 };
    TxnBeginAckMessage ack;

    begin.channel = CHANNEL;
    begin.idle_timeout_ms = idle_timeout_ms;
    begin.keepalive.period_ms = KEEPALIVE_PERIOD_MS;
    begin.keepalive.can_id = REQUEST_ID;
    begin.keepalive.payload_length = 3;
    begin.keepalive.payload[0] = 0x02;
    begin.keepalive.payload[1] = 0x3E;
    begin.keepalive.payload[2] = 0x80;
    TransactionRunner_Begin(&runner, &begin, INTERFACE_INDEX, 0, &ack);
    txn_id = ack.txn_id;
    return ack;
}

static void defineTemplate(const TxnTemplateMessage *template_message)
{
    TransactionRunner_DefineTemplate(&runner, template_message);
}

static TxnStep requestStep(uint8_t service)
{
    TxnStep step = { 0 };

    step.template_id = RESPONSE_TEMPLATE;
    step.can_id = REQUEST_ID;
    step.payload_length = 2;
    step.payload[0] = 0x01;
    step.payload[1] = service;
    return step;
}

static TxnStep transmitOnlyStep(uint8_t template_id, uint32_t can_id)
{
    TxnStep step = { 0 };

    step.template_id = template_id;
    step.can_id = can_id;
    step.payload_length = FRAME_PAYLOAD_MAX_CLASSIC;
    return step;
}

static TxnStep expectOnlyStep(uint8_t template_id)
{
    TxnStep step = { 0 };

    step.template_id = template_id;
    step.flags = TXN_STEP_FLAG_EXPECT_ONLY;
    return step;
}

static TxnStep messageRecord(uint8_t template_id, uint8_t flags, uint8_t length)
{
    TxnStep step = { 0 };
    uint8_t i;

    step.template_id = template_id;
    step.flags = flags;
    step.can_id = REQUEST_ID;
    step.payload_length = length;
    for(i=0; i<length; i++) {
        step.payload[i] = (uint8_t)(0x36 + i);
    }
    return step;
}

static void pushSteps(const TxnStep *steps, uint8_t step_count, uint32_t first_seq)
{
    pushStepsOfTransaction(txn_id, steps, step_count, first_seq);
}

static void pushLastSteps(const TxnStep *steps, uint8_t step_count, uint32_t first_seq)
{
    pushBatch(txn_id, steps, step_count, first_seq, TXN_STEPS_FLAG_END);
}

static void pushStepsOfTransaction(uint16_t other_txn_id, const TxnStep *steps, uint8_t step_count, uint32_t first_seq)
{
    pushBatch(other_txn_id, steps, step_count, first_seq, 0);
}

static void pushBatch(uint16_t other_txn_id, const TxnStep *steps, uint8_t step_count, uint32_t first_seq, uint8_t flags)
{
    TxnStepsMessage fixed_fields = { CHANNEL, 0, other_txn_id, first_seq, flags, NULL, 0 };
    TxnStepsMessage decoded;
    TxnStepsWriter writer;
    uint8_t buffer[MESSAGE_HEADER_SIZE + TXN_STEPS_BODY_SIZE_MAX];
    size_t encoded_size;
    uint8_t i;

    TxnStepsWriter_Init(&writer, &fixed_fields, buffer, sizeof(buffer));
    for(i=0; i<step_count; i++) {
        TxnStepsWriter_Append(&writer, &steps[i]);
    }
    encoded_size = TxnStepsWriter_Finish(&writer);
    TxnStepsMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);
    TransactionRunner_PushSteps(&runner, &decoded);
}

static void answer(uint32_t can_id, uint8_t byte0, uint8_t byte1, uint8_t byte2, uint8_t byte3)
{
    FrameMessage frame = { 0 };

    frame.can_id = can_id;
    frame.payload_length = 4;
    frame.payload[0] = byte0;
    frame.payload[1] = byte1;
    frame.payload[2] = byte2;
    frame.payload[3] = byte3;
    TransactionRunner_OnFrame(&runner, INTERFACE_INDEX, &frame);
}

static void answerFromNode(uint8_t node, uint16_t crc)
{
    answer(ACK_ID_BASE | node, 0x01, (uint8_t)(crc & 0xFF), (uint8_t)(crc >> 8), 0x00);
}

static void answerEcho(uint32_t can_id)
{
    FrameMessage frame = { 0 };

    frame.can_id = can_id;
    frame.payload_length = 2;
    frame.payload[1] = 0x76;
    frame.route_flags = FRAME_ROUTE_FLAG_ECHO;
    TransactionRunner_OnFrame(&runner, INTERFACE_INDEX, &frame);
}

static void answerBytes(std::initializer_list<uint8_t> bytes)
{
    FrameMessage frame = { 0 };

    frame.can_id = RESPONSE_ID;
    frame.payload_length = (uint8_t)bytes.size();
    memcpy(frame.payload, bytes.begin(), bytes.size());
    TransactionRunner_OnFrame(&runner, INTERFACE_INDEX, &frame);
}

static bool transmitFrame(void *context, uint8_t interface_index, const FrameMessage *frame)
{
    (void)context;
    (void)interface_index;

    if (!transmit_accepts) {
        transmit_count++;
        return false;
    }
    if (transmit_count < TRANSMITTED_MAX) {
        transmitted[transmit_count] = *frame;
    }
    transmit_count++;
    return true;
}

static void recordProgress(void *context, const TxnProgressMessage *progress)
{
    (void)context;

    last_progress = *progress;
    progress_count++;
}

static void recordStatus(void *context, const TxnStatusMessage *status)
{
    (void)context;

    last_status = *status;
    status_count++;
}

static void recordRosterReport(void *context, const TxnRosterReportMessage *report)
{
    (void)context;

    last_roster_report = *report;
    roster_report_count++;
}
