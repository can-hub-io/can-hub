#include <cest>
#include <cstring>

extern "C" {
#include "platform/linux/libcanhub/canhub_transactions.h"
}

#define REQUEST_ID 0x7E0
#define RESPONSE_ID 0x7E8
#define MESSAGE_SIZE 600
#define LAST_RECORD_SIZE (MESSAGE_SIZE - 2 * CANHUB_TRANSACTIONS_MESSAGE_RECORD_BYTES)
#define TEMPLATE_ID 3
#define EXPECT_VALUE 0x0576
#define EXTEND_TIMEOUT_MS 5000

static uint8_t data[CANHUB_ISOTP_MESSAGE_MAX + 1];
static TxnStep records[CANHUB_TRANSACTIONS_MESSAGE_RECORDS_MAX];

static CanHubTxnMessage messageOf(uint16_t length);
static CanHubTxnTemplate isotpTemplate(void);

describe("canhub_transactions", []() {
    describe("message to wire", []() {
        beforeEach([]() {
            uint16_t i;

            memset(records, 0, sizeof(records));
            for(i=0; i<sizeof(data); i++) {
                data[i] = (uint8_t)i;
            }
        });

        it("cuts a message into chained records, the expected value on the last", []() {
            CanHubTxnMessage message;
            uint8_t count;

            message = messageOf(MESSAGE_SIZE);
            message.flags = CANHUB_TXN_STEP_EXPECT_VALUE;
            message.expect_value = EXPECT_VALUE;

            count = CanHubTransactions_MessageToWire(&message, records);

            expect(count).toBe(3);
            expect(records[0].flags).toBe((uint8_t)(TXN_STEP_FLAG_MESSAGE | TXN_STEP_FLAG_CONTINUED));
            expect(records[0].payload_length).toBe(CANHUB_TRANSACTIONS_MESSAGE_RECORD_BYTES);
            expect(records[1].payload[0]).toBe(data[CANHUB_TRANSACTIONS_MESSAGE_RECORD_BYTES]);
            expect(records[2].flags).toBe((uint8_t)(TXN_STEP_FLAG_MESSAGE | TXN_STEP_FLAG_EXPECT_VALUE));
            expect(records[2].payload_length).toBe(LAST_RECORD_SIZE);
            expect(records[2].expect_value).toBe((uint64_t)EXPECT_VALUE);
            expect(records[2].can_id).toBe((uint32_t)REQUEST_ID);
            expect(records[2].template_id).toBe(TEMPLATE_ID);
        });

        it("fits the longest message in its records", []() {
            CanHubTxnMessage message;
            uint8_t count;

            message = messageOf(CANHUB_ISOTP_MESSAGE_MAX);

            count = CanHubTransactions_MessageToWire(&message, records);

            expect(count).toBe(CANHUB_TRANSACTIONS_MESSAGE_RECORDS_MAX);
        });

        it("refuses an empty message or one over 4095 bytes", []() {
            CanHubTxnMessage empty;
            CanHubTxnMessage too_long;
            uint8_t empty_count;
            uint8_t too_long_count;

            empty = messageOf(0);
            too_long = messageOf(CANHUB_ISOTP_MESSAGE_MAX + 1);

            empty_count = CanHubTransactions_MessageToWire(&empty, records);
            too_long_count = CanHubTransactions_MessageToWire(&too_long, records);

            expect(empty_count).toBe(0);
            expect(too_long_count).toBe(0);
        });
    });

    describe("template to wire", []() {
        it("carries the ISO-TP type and parameters", []() {
            CanHubTxnTemplate definition;
            TxnTemplateMessage message;
            bool converted;

            definition = isotpTemplate();

            converted = CanHubTransactions_TemplateToWire(&definition, &message);

            expect(converted).toBe(true);
            expect(message.type).toBe((uint8_t)kTXN_TEMPLATE_TYPE_ISOTP);
            expect(message.isotp.response_id).toBe((uint32_t)RESPONSE_ID);
            expect(message.isotp.flags).toBe((uint8_t)CANHUB_ISOTP_FLAG_PADDING);
            expect(message.isotp.tx_data_length).toBe(FRAME_PAYLOAD_MAX_CLASSIC);
        });

        it("carries the wait after a response pending", []() {
            CanHubTxnTemplate definition;
            TxnTemplateMessage message;

            definition = isotpTemplate();
            definition.extend_timeout_ms = EXTEND_TIMEOUT_MS;

            CanHubTransactions_TemplateToWire(&definition, &message);

            expect(message.extend_timeout_ms).toBe((uint32_t)EXTEND_TIMEOUT_MS);
        });
    });
});

static CanHubTxnMessage messageOf(uint16_t length)
{
    CanHubTxnMessage message;

    memset(&message, 0, sizeof(message));
    message.struct_size = sizeof(message);
    message.can_id = REQUEST_ID;
    message.template_id = TEMPLATE_ID;
    message.length = length;
    message.data = data;
    return message;
}

static CanHubTxnTemplate isotpTemplate(void)
{
    CanHubTxnTemplate definition;

    memset(&definition, 0, sizeof(definition));
    definition.struct_size = sizeof(definition);
    definition.template_id = TEMPLATE_ID;
    definition.advance_mode = CANHUB_TXN_ADVANCE_COUNT;
    definition.count_k = 1;
    definition.timeout_ms = 1000;
    definition.type = CANHUB_TXN_TYPE_ISOTP;
    definition.isotp.response_id = RESPONSE_ID;
    definition.isotp.flags = CANHUB_ISOTP_FLAG_PADDING;
    definition.isotp.padding = 0xCC;
    definition.isotp.tx_data_length = FRAME_PAYLOAD_MAX_CLASSIC;
    return definition;
}
