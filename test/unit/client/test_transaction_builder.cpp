#include <cest>
#include <cstring>

extern "C" {
#include "client/transaction_builder.h"
}

#define REQUEST_ID 0x7E0
#define RESPONSE_ID 0x7E8
#define EXTENDED_REQUEST_ID 0x18DA10F1
#define EXTENDED_RESPONSE_BASE 0x18DAF100
#define EXTENDED_NODE_BITS 0xFF
#define ALIVE_BASE 0x700
#define NODE_BITS 0x00F
#define NODES 16
#define TIMEOUT_MS 50
#define PENDING_TIMEOUT_MS 5000
#define IDLE_TIMEOUT_MS 60000
#define KEEPALIVE_EVERY_MS 2000
#define GAP_US 10000
#define FD_LENGTH 12
#define BAD_FD_LENGTH 9
#define MESSAGE_SIZE 600
#define LAST_RECORD_SIZE (MESSAGE_SIZE - 2 * TRANSACTION_BUILDER_MESSAGE_RECORD_BYTES)
#define EFF_FLAG 0x80000000u

static TransactionBuilder builder;
static TransactionBuild build;
static uint8_t request[] = { 0x02, 0x10, 0x03 };
static uint8_t fd_request[FD_LENGTH];
static uint8_t message[CANHUB_ISOTP_MESSAGE_MAX + 1];
static uint32_t nodes[NODES];
static CanHubTxnMatch negative;
static CanHubTxnMatch busy;

static void resetBuilder(void);
static CanHubTxnResponse positiveResponse(void);
static CanHubTxnResponse aliveResponse(void);
static CanHubTxnResponse udsResponse(void);
static CanHubTxnStep sendStep(const CanHubTxnResponse *response);
static CanHubTxnIsotpLink ecuLink(void);
static bool step(const CanHubTxnStep *definition);
static const char *refusalOf(const CanHubTxnStep *definition);

describe("transaction_builder", []() {
    describe("begin", []() {
        beforeEach([]() {
            resetBuilder();
        });

        it("asks for the agent defaults without options", []() {
            TxnBeginMessage begin;
            const char *error = NULL;
            bool built;

            built = TransactionBuilder_Begin(NULL, &begin, &error);

            expect(built).toBe(true);
            expect(begin.flags).toBe(0);
            expect(begin.idle_timeout_ms).toBe((uint32_t)0);
            expect(begin.keepalive.period_ms).toBe((uint32_t)0);
        });

        it("carries exclusive, the idle timeout and the keepalive frame", []() {
            CanHubTxnKeepalive keepalive = { { REQUEST_ID, false }, { 0x02, 0x3E, 0x80 }, 3, KEEPALIVE_EVERY_MS };
            CanHubTxnOptions options = { true, IDLE_TIMEOUT_MS, &keepalive, false, NULL, NULL };
            TxnBeginMessage begin;
            const char *error = NULL;
            bool built;

            built = TransactionBuilder_Begin(&options, &begin, &error);

            expect(built).toBe(true);
            expect(begin.flags).toBe((uint8_t)TXN_BEGIN_FLAG_EXCLUSIVE);
            expect(begin.idle_timeout_ms).toBe((uint32_t)IDLE_TIMEOUT_MS);
            expect(begin.keepalive.period_ms).toBe((uint32_t)KEEPALIVE_EVERY_MS);
            expect(begin.keepalive.can_id).toBe((uint32_t)REQUEST_ID);
            expect(begin.keepalive.payload_length).toBe(3);
            expect(begin.keepalive.payload[1]).toBe(0x3E);
        });

        it("refuses options without an idle timeout", []() {
            CanHubTxnOptions options = { true, 0, NULL, false, NULL, NULL };
            TxnBeginMessage begin;
            const char *error = NULL;
            bool built;

            built = TransactionBuilder_Begin(&options, &begin, &error);

            expect(built).toBe(false);
            expect(error != NULL).toBe(true);
        });

        it("refuses a keepalive without a period", []() {
            CanHubTxnKeepalive keepalive = { { REQUEST_ID, false }, { 0x3E }, 1, 0 };
            CanHubTxnOptions options = { false, IDLE_TIMEOUT_MS, &keepalive, false, NULL, NULL };
            TxnBeginMessage begin;
            const char *error = NULL;
            bool built;

            built = TransactionBuilder_Begin(&options, &begin, &error);

            expect(built).toBe(false);
        });
    });

    describe("templates", []() {
        beforeEach([]() {
            resetBuilder();
        });

        it("turns a response into a counting template on its id and bytes", []() {
            CanHubTxnResponse response = positiveResponse();
            CanHubTxnStep definition = sendStep(&response);
            bool built;

            built = step(&definition);

            expect(built).toBe(true);
            expect(build.defines_template).toBe(true);
            expect(build.template_message.advance_mode).toBe((uint8_t)kTXN_ADVANCE_MODE_COUNT);
            expect(build.template_message.count_k).toBe(1);
            expect(build.template_message.timeout_ms).toBe((uint32_t)TIMEOUT_MS);
            expect(build.template_message.patterns_present).toBe((uint8_t)(1u << kTXN_PATTERN_ADVANCE));
            expect(build.template_message.patterns[kTXN_PATTERN_ADVANCE].can_id).toBe((uint32_t)RESPONSE_ID);
            expect(build.template_message.patterns[kTXN_PATTERN_ADVANCE].can_mask).toBe((uint32_t)(0x7FF | EFF_FLAG));
            expect(build.template_message.patterns[kTXN_PATTERN_ADVANCE].payload_mask).toBe((uint64_t)0xFF00);
            expect(build.template_message.patterns[kTXN_PATTERN_ADVANCE].payload_value).toBe((uint64_t)0x5000);
        });

        it("defines a template once and finds it again for an equal response built anew", []() {
            CanHubTxnResponse first = positiveResponse();
            CanHubTxnResponse second = positiveResponse();
            CanHubTxnStep first_step = sendStep(&first);
            CanHubTxnStep second_step = sendStep(&second);
            bool first_defined;
            uint8_t first_template;

            step(&first_step);
            first_defined = build.defines_template;
            first_template = build.records[0].template_id;
            step(&second_step);

            expect(first_defined).toBe(true);
            expect(build.defines_template).toBe(false);
            expect(build.records[0].template_id).toBe(first_template);
        });

        it("defines another template for a response reused with other content", []() {
            CanHubTxnResponse response = positiveResponse();
            CanHubTxnStep definition = sendStep(&response);
            uint8_t first_template;

            step(&definition);
            first_template = build.records[0].template_id;
            response.timeout_ms = TIMEOUT_MS * 2;
            step(&definition);

            expect(build.defines_template).toBe(true);
            expect(build.records[0].template_id != first_template).toBe(true);
        });

        it("maps failure to the abort pattern and pending to the extend one with its own timeout", []() {
            CanHubTxnResponse response = udsResponse();
            CanHubTxnStep definition = sendStep(&response);

            step(&definition);

            expect(build.template_message.patterns_present).toBe((uint8_t)0x07);
            expect(build.template_message.patterns[kTXN_PATTERN_ABORT].payload_value).toBe((uint64_t)0x7F00);
            expect(build.template_message.patterns[kTXN_PATTERN_EXTEND].payload_mask).toBe((uint64_t)0xFF00FF00);
            expect(build.template_message.extend_timeout_ms).toBe((uint32_t)PENDING_TIMEOUT_MS);
        });

        it("flags continue on timeout as soft and on mismatch abort", []() {
            CanHubTxnResponse response = positiveResponse();
            CanHubTxnStep definition = sendStep(&response);

            response.continue_on_timeout = true;
            response.on_mismatch_abort = true;

            step(&definition);

            expect(build.template_message.flags).toBe((uint8_t)(TXN_TEMPLATE_FLAG_SOFT | TXN_TEMPLATE_FLAG_MISMATCH_ABORT));
        });

        it("compares an extended id and leaves its ignored bits free", []() {
            CanHubTxnResponse response = positiveResponse();
            CanHubTxnStep definition = sendStep(&response);

            response.success.id = CANHUB_EXTENDED_ID(EXTENDED_RESPONSE_BASE);
            response.success.id_ignored_bits = EXTENDED_NODE_BITS;

            step(&definition);

            expect(build.template_message.patterns[kTXN_PATTERN_ADVANCE].can_id).toBe((uint32_t)(EXTENDED_RESPONSE_BASE | EFF_FLAG));
            expect(build.template_message.patterns[kTXN_PATTERN_ADVANCE].can_mask).toBe((uint32_t)(0x1FFFFF00 | EFF_FLAG));
        });

        it("waits for nothing on a step without response, keeping its gap", []() {
            CanHubTxnStep definition = sendStep(NULL);

            definition.gap_us = GAP_US;

            step(&definition);

            expect(build.template_message.advance_mode).toBe((uint8_t)kTXN_ADVANCE_MODE_NONE);
            expect(build.template_message.gap_us).toBe((uint32_t)GAP_US);
        });

        it("takes id 0 as the id 0x000", []() {
            CanHubTxnResponse response = positiveResponse();
            CanHubTxnStep definition = sendStep(&response);
            bool built;

            response.success.id.value = 0;

            built = step(&definition);

            expect(built).toBe(true);
            expect(build.template_message.patterns[kTXN_PATTERN_ADVANCE].can_id).toBe((uint32_t)0);
            expect(build.template_message.patterns[kTXN_PATTERN_ADVANCE].can_mask).toBe((uint32_t)(0x7FF | EFF_FLAG));
        });

        it("refuses a 17th way of waiting", []() {
            CanHubTxnResponse response = positiveResponse();
            CanHubTxnStep definition = sendStep(&response);
            bool sixteenth;
            bool seventeenth;
            uint32_t i;

            for(i=1; i<TXN_TEMPLATES_MAX; i++) {
                response.timeout_ms = i;
                step(&definition);
            }
            response.timeout_ms = TXN_TEMPLATES_MAX;
            sixteenth = step(&definition);
            response.timeout_ms = TXN_TEMPLATES_MAX + 1;
            seventeenth = step(&definition);

            expect(sixteenth).toBe(true);
            expect(seventeenth).toBe(false);
        });
    });

    describe("steps", []() {
        beforeEach([]() {
            resetBuilder();
        });

        it("sends a classic frame on the step's id with its data", []() {
            CanHubTxnResponse response = positiveResponse();
            CanHubTxnStep definition = sendStep(&response);

            step(&definition);

            expect(build.record_count).toBe(1);
            expect(build.records[0].can_id).toBe((uint32_t)REQUEST_ID);
            expect(build.records[0].payload_length).toBe(3);
            expect(build.records[0].payload[1]).toBe(0x10);
            expect(build.records[0].frame_flags).toBe(0);
            expect(build.records[0].flags).toBe(0);
        });

        it("sends more than 8 bytes as CAN FD, with bit rate switch when the options ask", []() {
            CanHubTxnStep definition = sendStep(NULL);

            TransactionBuilder_Init(&builder, true);
            definition.data = fd_request;
            definition.length = FD_LENGTH;

            step(&definition);

            expect(build.records[0].frame_flags).toBe((uint8_t)(FRAME_FLAG_FD | FRAME_FLAG_BRS));
            expect(build.records[0].payload_length).toBe(FD_LENGTH);
        });

        it("waits without sending when the step has no data", []() {
            CanHubTxnResponse response = positiveResponse();
            CanHubTxnStep definition;

            memset(&definition, 0, sizeof(definition));
            definition.response = &response;

            step(&definition);

            expect(build.records[0].flags).toBe((uint8_t)TXN_STEP_FLAG_EXPECT_ONLY);
            expect(build.records[0].can_id).toBe((uint32_t)0);
        });

        it("sends an empty frame for data of length 0", []() {
            CanHubTxnStep definition = sendStep(NULL);

            definition.length = 0;

            step(&definition);

            expect(build.records[0].flags).toBe(0);
            expect(build.records[0].payload_length).toBe(0);
        });

        it("adds where's bytes to the template mask and its values to the step", []() {
            CanHubTxnResponse response = positiveResponse();
            CanHubTxnMatch where;
            CanHubTxnStep definition = sendStep(&response);

            memset(&where, 0, sizeof(where));
            where.id = response.success.id;
            where.bytes[2] = { 0xFF, 0xF1 };
            definition.where = &where;

            step(&definition);

            expect(build.template_message.patterns[kTXN_PATTERN_ADVANCE].payload_mask).toBe((uint64_t)0xFFFF00);
            expect(build.template_message.patterns[kTXN_PATTERN_ADVANCE].payload_value).toBe((uint64_t)0x5000);
            expect(build.records[0].flags).toBe((uint8_t)TXN_STEP_FLAG_EXPECT_VALUE);
            expect(build.records[0].expect_value).toBe((uint64_t)0xF15000);
        });

        it("shares the template of where steps that only differ in values", []() {
            CanHubTxnResponse response = positiveResponse();
            CanHubTxnMatch where;
            CanHubTxnStep definition = sendStep(&response);
            uint8_t first_template;

            where = response.success;
            where.bytes[2] = { 0xFF, 0x01 };
            definition.where = &where;
            step(&definition);
            first_template = build.records[0].template_id;
            where.bytes[2].value = 0x02;

            step(&definition);

            expect(build.defines_template).toBe(false);
            expect(build.records[0].template_id).toBe(first_template);
            expect(build.records[0].expect_value).toBe((uint64_t)0x025000);
        });

        it("replaces the response's id with where's", []() {
            CanHubTxnResponse response = positiveResponse();
            CanHubTxnMatch where;
            CanHubTxnStep definition = sendStep(&response);

            response.success.id_ignored_bits = NODE_BITS;
            where = response.success;
            where.id.value = 0x585;
            where.id_ignored_bits = 0;
            definition.where = &where;

            step(&definition);

            expect(build.template_message.patterns[kTXN_PATTERN_ADVANCE].can_id).toBe((uint32_t)0x585);
            expect(build.template_message.patterns[kTXN_PATTERN_ADVANCE].can_mask).toBe((uint32_t)(0x7FF | EFF_FLAG));
        });

        it("refuses a frame length CAN FD does not have", []() {
            CanHubTxnStep definition = sendStep(NULL);

            definition.data = fd_request;
            definition.length = BAD_FD_LENGTH;

            expect(step(&definition)).toBe(false);
        });

        it("refuses a standard id over 11 bits", []() {
            CanHubTxnStep definition = sendStep(NULL);

            definition.id = CANHUB_STANDARD_ID(0x800);

            expect(step(&definition)).toBe(false);
        });

        it("refuses a step that neither sends nor waits", []() {
            CanHubTxnStep definition;

            memset(&definition, 0, sizeof(definition));

            expect(step(&definition)).toBe(false);
        });

        it("refuses where without a response", []() {
            CanHubTxnMatch where;
            CanHubTxnStep definition = sendStep(NULL);

            memset(&where, 0, sizeof(where));
            definition.where = &where;

            expect(step(&definition)).toBe(false);
        });
    });

    describe("responses", []() {
        beforeEach([]() {
            resetBuilder();
        });

        it("needs a timeout", []() {
            CanHubTxnResponse response = positiveResponse();
            CanHubTxnStep definition = sendStep(&response);

            response.timeout_ms = 0;

            expect(refusalOf(&definition) != NULL).toBe(true);
        });

        it("needs a timeout after pending when it has pending", []() {
            CanHubTxnResponse response = udsResponse();
            CanHubTxnStep definition = sendStep(&response);

            response.timeout_after_pending_ms = 0;

            expect(refusalOf(&definition) != NULL).toBe(true);
        });

        it("refuses ignored bits over the id width", []() {
            CanHubTxnResponse response = positiveResponse();
            CanHubTxnStep definition = sendStep(&response);

            response.success.id_ignored_bits = 0x800;

            expect(refusalOf(&definition) != NULL).toBe(true);
        });
    });

    describe("roster", []() {
        beforeEach([]() {
            resetBuilder();
        });

        it("defines the roster with the first response that names nodes, told by the ignored bits", []() {
            CanHubTxnResponse response = aliveResponse();
            CanHubTxnStep definition = sendStep(&response);

            step(&definition);

            expect(build.defines_roster).toBe(true);
            expect(build.roster.origin.source).toBe((uint8_t)kTXN_ORIGIN_SOURCE_CAN_ID);
            expect(build.roster.origin.mask).toBe((uint32_t)NODE_BITS);
            expect(build.roster.origin.shift).toBe(0);
            expect(build.roster.roster_count).toBe(NODES);
            expect(build.roster.roster[NODES - 1]).toBe((uint32_t)(NODES - 1));
            expect(build.template_message.advance_mode).toBe((uint8_t)kTXN_ADVANCE_MODE_ROSTER_ALL);
            expect(build.template_message.patterns[kTXN_PATTERN_ADVANCE].can_mask).toBe((uint32_t)(0x7F0 | EFF_FLAG));
        });

        it("shifts nodes told by higher bits of the id", []() {
            CanHubTxnResponse response = aliveResponse();
            CanHubTxnStep definition = sendStep(&response);

            response.success.id = CANHUB_EXTENDED_ID(0x18FF0051);
            response.success.id_ignored_bits = 0xFF00;

            step(&definition);

            expect(build.roster.origin.mask).toBe((uint32_t)0xFF00);
            expect(build.roster.origin.shift).toBe(8);
        });

        it("tells nodes by a data byte", []() {
            CanHubTxnResponse response = aliveResponse();
            CanHubTxnStep definition = sendStep(&response);

            response.success.id_ignored_bits = 0;
            response.node_from = CANHUB_TXN_NODE_IN_BYTE;
            response.node_byte = 2;

            step(&definition);

            expect(build.roster.origin.source).toBe((uint8_t)kTXN_ORIGIN_SOURCE_PAYLOAD);
            expect(build.roster.origin.payload_offset).toBe(2);
            expect(build.roster.origin.mask).toBe((uint32_t)0xFF);
        });

        it("waits for node_count - allowed_missing nodes", []() {
            CanHubTxnResponse response = aliveResponse();
            CanHubTxnStep definition = sendStep(&response);

            response.allowed_missing = 4;

            step(&definition);

            expect(build.template_message.advance_mode).toBe((uint8_t)kTXN_ADVANCE_MODE_ROSTER_K);
            expect(build.template_message.count_k).toBe(NODES - 4);
        });

        it("defines the roster once for every response that names the same nodes", []() {
            CanHubTxnResponse alive = aliveResponse();
            CanHubTxnResponse other = aliveResponse();
            CanHubTxnStep first = sendStep(&alive);
            CanHubTxnStep second = sendStep(&other);
            bool built;

            other.timeout_ms = TIMEOUT_MS * 2;
            step(&first);

            built = step(&second);

            expect(built).toBe(true);
            expect(build.defines_roster).toBe(false);
            expect(build.defines_template).toBe(true);
        });

        it("refuses a second, different set of nodes", []() {
            CanHubTxnResponse alive = aliveResponse();
            CanHubTxnResponse fewer = aliveResponse();
            CanHubTxnStep first = sendStep(&alive);
            CanHubTxnStep second = sendStep(&fewer);

            fewer.node_count = NODES - 1;
            step(&first);

            expect(refusalOf(&second) != NULL).toBe(true);
        });

        it("refuses a node that does not fit the ignored bits", []() {
            CanHubTxnResponse response = aliveResponse();
            CanHubTxnStep definition = sendStep(&response);

            nodes[NODES - 1] = 0x10;

            expect(refusalOf(&definition) != NULL).toBe(true);
        });

        it("refuses nodes told by the id without ignored bits", []() {
            CanHubTxnResponse response = aliveResponse();
            CanHubTxnStep definition = sendStep(&response);

            response.success.id_ignored_bits = 0;

            expect(refusalOf(&definition) != NULL).toBe(true);
        });

        it("refuses allowing every node to be missing", []() {
            CanHubTxnResponse response = aliveResponse();
            CanHubTxnStep definition = sendStep(&response);

            response.allowed_missing = NODES;

            expect(refusalOf(&definition) != NULL).toBe(true);
        });

        it("refuses where changing the id of a response with nodes", []() {
            CanHubTxnResponse response = aliveResponse();
            CanHubTxnMatch where;
            CanHubTxnStep definition = sendStep(&response);

            where = response.success;
            where.id_ignored_bits = 0;
            definition.where = &where;

            expect(refusalOf(&definition) != NULL).toBe(true);
        });
    });

    describe("ISO-TP", []() {
        beforeEach([]() {
            resetBuilder();
        });

        it("cuts a message into chained records on the link's tx_id, the expect value on the last", []() {
            CanHubTxnIsotpLink link = ecuLink();
            CanHubTxnResponse response = positiveResponse();
            CanHubTxnMatch where;
            CanHubTxnStep definition = sendStep(&response);

            response.success.bytes[1] = { 0, 0 };
            response.success.bytes[0] = { 0xFF, 0x76 };
            where = response.success;
            where.bytes[1] = { 0xFF, 0x05 };
            definition.isotp = &link;
            definition.data = message;
            definition.length = MESSAGE_SIZE;
            definition.where = &where;

            step(&definition);

            expect(build.record_count).toBe(3);
            expect(build.records[0].flags).toBe((uint8_t)(TXN_STEP_FLAG_MESSAGE | TXN_STEP_FLAG_CONTINUED));
            expect(build.records[0].can_id).toBe((uint32_t)REQUEST_ID);
            expect(build.records[1].payload[0]).toBe(message[TRANSACTION_BUILDER_MESSAGE_RECORD_BYTES]);
            expect(build.records[2].flags).toBe((uint8_t)(TXN_STEP_FLAG_MESSAGE | TXN_STEP_FLAG_EXPECT_VALUE));
            expect(build.records[2].payload_length).toBe(LAST_RECORD_SIZE);
            expect(build.records[2].expect_value).toBe((uint64_t)0x0576);
        });

        it("defines an ISO-TP template with the link's parameters", []() {
            CanHubTxnIsotpLink link = ecuLink();
            CanHubTxnResponse response = positiveResponse();
            CanHubTxnStep definition = sendStep(&response);

            definition.isotp = &link;

            step(&definition);

            expect(build.template_message.type).toBe((uint8_t)kTXN_TEMPLATE_TYPE_ISOTP);
            expect(build.template_message.isotp.response_id).toBe((uint32_t)RESPONSE_ID);
            expect(build.template_message.isotp.flags).toBe((uint8_t)TXN_ISOTP_FLAG_PADDING);
            expect(build.template_message.isotp.padding).toBe(0xCC);
            expect(build.template_message.isotp.tx_data_length).toBe(8);
        });

        it("fits the longest message in its records", []() {
            CanHubTxnIsotpLink link = ecuLink();
            CanHubTxnStep definition = sendStep(NULL);

            definition.isotp = &link;
            definition.data = message;
            definition.length = CANHUB_ISOTP_MESSAGE_MAX;

            step(&definition);

            expect(build.record_count).toBe(TRANSACTION_BUILDER_RECORDS_MAX);
        });

        it("waits for a message without sending one on the request id", []() {
            CanHubTxnIsotpLink link = ecuLink();
            CanHubTxnResponse response = positiveResponse();
            CanHubTxnStep definition;

            memset(&definition, 0, sizeof(definition));
            definition.id = link.tx_id;
            definition.isotp = &link;
            definition.response = &response;

            step(&definition);

            expect(build.records[0].flags).toBe((uint8_t)TXN_STEP_FLAG_EXPECT_ONLY);
            expect(build.records[0].can_id).toBe((uint32_t)REQUEST_ID);
        });

        it("refuses a message over 4095 bytes", []() {
            CanHubTxnIsotpLink link = ecuLink();
            CanHubTxnStep definition = sendStep(NULL);

            definition.isotp = &link;
            definition.data = message;
            definition.length = CANHUB_ISOTP_MESSAGE_MAX + 1;

            expect(step(&definition)).toBe(false);
        });

        it("refuses a step id other than the link's tx_id", []() {
            CanHubTxnIsotpLink link = ecuLink();
            CanHubTxnStep definition = sendStep(NULL);

            definition.isotp = &link;
            definition.id = CANHUB_STANDARD_ID(REQUEST_ID + 1);

            expect(step(&definition)).toBe(false);
        });

        it("refuses a response on an id other than the link's rx_id", []() {
            CanHubTxnIsotpLink link = ecuLink();
            CanHubTxnResponse response = positiveResponse();
            CanHubTxnStep definition = sendStep(&response);

            definition.isotp = &link;
            response.success.id.value = RESPONSE_ID + 1;

            expect(refusalOf(&definition) != NULL).toBe(true);
        });

        it("refuses a CAN FD link without a valid data length", []() {
            CanHubTxnIsotpLink link = ecuLink();
            CanHubTxnStep definition = sendStep(NULL);

            link.fd = true;
            link.fd_data_length = BAD_FD_LENGTH;
            definition.isotp = &link;

            expect(step(&definition)).toBe(false);
        });
    });
});

static void resetBuilder(void)
{
    uint32_t i;

    TransactionBuilder_Init(&builder, false);
    memset(&build, 0, sizeof(build));
    for(i=0; i<NODES; i++) {
        nodes[i] = i;
    }
    for(i=0; i<sizeof(message); i++) {
        message[i] = (uint8_t)i;
    }
    memset(&negative, 0, sizeof(negative));
    negative.id = CANHUB_STANDARD_ID(RESPONSE_ID);
    negative.bytes[1] = { 0xFF, 0x7F };
    busy = negative;
    busy.bytes[3] = { 0xFF, 0x78 };
}

static CanHubTxnResponse positiveResponse(void)
{
    CanHubTxnResponse response;

    memset(&response, 0, sizeof(response));
    response.success.id = CANHUB_STANDARD_ID(RESPONSE_ID);
    response.success.bytes[1] = { 0xFF, 0x50 };
    response.timeout_ms = TIMEOUT_MS;
    return response;
}

static CanHubTxnResponse aliveResponse(void)
{
    CanHubTxnResponse response;

    memset(&response, 0, sizeof(response));
    response.success.id = CANHUB_STANDARD_ID(ALIVE_BASE);
    response.success.id_ignored_bits = NODE_BITS;
    response.nodes = nodes;
    response.node_count = NODES;
    response.timeout_ms = TIMEOUT_MS;
    response.continue_on_timeout = true;
    return response;
}

static CanHubTxnResponse udsResponse(void)
{
    CanHubTxnResponse response = positiveResponse();

    response.failure = &negative;
    response.pending = &busy;
    response.timeout_after_pending_ms = PENDING_TIMEOUT_MS;
    return response;
}

static CanHubTxnStep sendStep(const CanHubTxnResponse *response)
{
    CanHubTxnStep definition;

    memset(&definition, 0, sizeof(definition));
    definition.id = CANHUB_STANDARD_ID(REQUEST_ID);
    definition.data = request;
    definition.length = sizeof(request);
    definition.response = response;
    return definition;
}

static CanHubTxnIsotpLink ecuLink(void)
{
    CanHubTxnIsotpLink link;

    memset(&link, 0, sizeof(link));
    link.tx_id = CANHUB_STANDARD_ID(REQUEST_ID);
    link.rx_id = CANHUB_STANDARD_ID(RESPONSE_ID);
    link.padded = true;
    link.padding = 0xCC;
    return link;
}

static bool step(const CanHubTxnStep *definition)
{
    return TransactionBuilder_Step(&builder, definition, &build);
}

static const char *refusalOf(const CanHubTxnStep *definition)
{
    if (TransactionBuilder_Step(&builder, definition, &build)) {
        return NULL;
    }
    return TransactionBuilder_Error(&builder);
}
