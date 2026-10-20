#include <cest>
#include <cstring>
#include <initializer_list>

extern "C" {
#include "platform/linux/libcanhub/canhub_internal.h"
#include "platform/linux/libcanhub/canhub_uds.h"
}

#define REQUEST_ID 0x7E0
#define RESPONSE_ID 0x7E8
#define P2_MS 150
#define P2_EXTENDED_MS 5000
#define MESSAGES_MAX 16
#define TEMPLATES_PER_BEGIN (CANHUB_UDS_EXPECT_MAX + 1)
#define IMAGE_SIZE 9000
#define BLOCK_SIZE 4000
#define LAST_BLOCK_SIZE (IMAGE_SIZE - 2 * BLOCK_SIZE)

static CanHubUds uds;
static CanHubTxnBegin begun;
static CanHubTxnTemplate templates[TEMPLATES_PER_BEGIN];
static uint8_t template_count;
static CanHubTxnMessage messages[MESSAGES_MAX];
static uint8_t message_heads[MESSAGES_MAX][2];
static uint8_t message_count;
static uint8_t image[IMAGE_SIZE];

static void resetUds(void);
static bool requestMessage(std::initializer_list<uint8_t> request, CanHubTxnMessage *message);

describe("canhub_uds", []() {
    describe("templates", []() {
        beforeEach([]() {
            resetUds();
        });

        it("expects the start of the answer under its length, a 7F aborting and 7F xx 78 waiting P2*", []() {
            CanHubTxnTemplate definition;

            CanHubUds_Template(&uds, 2, &definition);

            expect(definition.template_id).toBe(2);
            expect(definition.type).toBe((uint8_t)CANHUB_TXN_TYPE_ISOTP);
            expect(definition.advance_mode).toBe((uint8_t)CANHUB_TXN_ADVANCE_COUNT);
            expect(definition.flags).toBe((uint8_t)CANHUB_TXN_TEMPLATE_MISMATCH_ABORT);
            expect(definition.timeout_ms).toBe((uint32_t)P2_MS);
            expect(definition.extend_timeout_ms).toBe((uint32_t)P2_EXTENDED_MS);
            expect(definition.patterns[CANHUB_TXN_PATTERN_ADVANCE].payload_mask).toBe((uint64_t)0xFFFF);
            expect(definition.patterns[CANHUB_TXN_PATTERN_ABORT].payload_value).toBe((uint64_t)0x7F);
            expect(definition.patterns[CANHUB_TXN_PATTERN_EXTEND].payload_value).toBe((uint64_t)0x78007F);
            expect(definition.isotp.response_id).toBe((uint32_t)RESPONSE_ID);
        });

        it("does not wait in the template of answers that are suppressed", []() {
            CanHubTxnTemplate definition;

            CanHubUds_Template(&uds, CANHUB_UDS_NO_ANSWER_TEMPLATE, &definition);

            expect(definition.advance_mode).toBe((uint8_t)CANHUB_TXN_ADVANCE_NONE);
            expect(definition.patterns_present).toBe(0);
        });
    });

    describe("requests", []() {
        beforeEach([]() {
            resetUds();
        });

        it("expects the service id plus 0x40 when nothing else is asked", []() {
            CanHubTxnMessage message;
            bool built;

            built = requestMessage({ 0x10, 0x02 }, &message);

            expect(built).toBe(true);
            expect(message.can_id).toBe((uint32_t)REQUEST_ID);
            expect(message.template_id).toBe(1);
            expect(message.flags).toBe((uint8_t)CANHUB_TXN_STEP_EXPECT_VALUE);
            expect(message.expect_value).toBe((uint64_t)0x50);
        });

        it("waits for no answer when the sub-function suppresses it", []() {
            CanHubTxnMessage message;

            requestMessage({ 0x3E, 0x80 }, &message);

            expect(message.template_id).toBe(CANHUB_UDS_NO_ANSWER_TEMPLATE);
            expect(message.flags).toBe(0);
        });

        it("keeps the suppress bit as data for a service without sub-function", []() {
            CanHubTxnMessage message;

            requestMessage({ 0x22, 0xF1, 0x90 }, &message);

            expect(message.template_id).toBe(1);
        });

        it("refuses an expected answer that is not this service's positive one", []() {
            CanHubTxnMessage message;
            uint8_t request[] = { 0x31, 0x01, 0xFF, 0x01 };
            uint8_t expect_wrong[] = { 0x72, 0x01 };
            bool built;

            built = CanHubUds_Message(&uds, request, sizeof(request), expect_wrong, sizeof(expect_wrong), &message);

            expect(built).toBe(false);
        });
    });

    describe("conversation", []() {
        beforeEach([]() {
            resetUds();
        });

        it("begins with ISO-TP steps and every template a request can use", []() {
            int32_t result;

            uds.flags = CANHUB_TXN_FLAG_EXCLUSIVE;

            result = canhub_uds_begin(NULL, &uds, 1000);

            expect(result).toBe(CANHUB_OK);
            expect(begun.flags).toBe((uint8_t)(CANHUB_TXN_FLAG_EXCLUSIVE | CANHUB_TXN_FLAG_ISOTP));
            expect(template_count).toBe(TEMPLATES_PER_BEGIN);
            expect(templates[CANHUB_UDS_EXPECT_MAX].template_id).toBe(CANHUB_UDS_EXPECT_MAX);
        });

        it("transfers data in counted blocks, each answered with its counter", []() {
            int32_t result;

            result = canhub_uds_transfer(NULL, &uds, image, IMAGE_SIZE, BLOCK_SIZE, -1);

            expect(result).toBe(CANHUB_OK);
            expect(message_count).toBe(3);
            expect(messages[0].length).toBe(BLOCK_SIZE + 2);
            expect(messages[2].length).toBe(LAST_BLOCK_SIZE + 2);
            expect(message_heads[1][0]).toBe(0x36);
            expect(message_heads[1][1]).toBe(2);
            expect(messages[1].expect_value).toBe((uint64_t)0x0276);
            expect(messages[1].template_id).toBe(2);
        });

        it("reads the code of a negative answer from the result", []() {
            CanHubTxnResult result;
            uint8_t code;

            memset(&result, 0, sizeof(result));
            result.has_rx_frame = 1;
            result.rx_frame.length = 3;
            result.rx_frame.payload[0] = 0x7F;
            result.rx_frame.payload[1] = 0x36;
            result.rx_frame.payload[2] = 0x73;

            code = canhub_uds_negative_code(&result);

            expect(code).toBe(0x73);
        });
    });
});

static void resetUds(void)
{
    memset(&uds, 0, sizeof(uds));
    uds.struct_size = sizeof(uds);
    uds.request_id = REQUEST_ID;
    uds.p2_ms = P2_MS;
    uds.p2_extended_ms = P2_EXTENDED_MS;
    uds.transport.response_id = RESPONSE_ID;
    uds.transport.flags = CANHUB_ISOTP_FLAG_PADDING;
    uds.transport.padding = 0xCC;
    uds.transport.tx_data_length = 8;
    memset(&begun, 0, sizeof(begun));
    template_count = 0;
    message_count = 0;
}

static bool requestMessage(std::initializer_list<uint8_t> request, CanHubTxnMessage *message)
{
    static uint8_t bytes[CANHUB_ISOTP_MESSAGE_MAX];

    memcpy(bytes, request.begin(), request.size());
    return CanHubUds_Message(&uds, bytes, (uint16_t)request.size(), NULL, 0, message);
}

extern "C" {

int32_t canhub_txn_begin(CanHubSession *session, const CanHubTxnBegin *begin, int32_t timeout_ms)
{
    begun = *begin;
    return CANHUB_OK;
}

int32_t canhub_txn_template(CanHubSession *session, const CanHubTxnTemplate *template_definition, int32_t timeout_ms)
{
    if (template_count < TEMPLATES_PER_BEGIN) {
        templates[template_count] = *template_definition;
    }
    template_count++;
    return CANHUB_OK;
}

int32_t canhub_txn_send_message(CanHubSession *session, const CanHubTxnMessage *message, int32_t timeout_ms)
{
    if (message_count < MESSAGES_MAX) {
        messages[message_count] = *message;
        message_heads[message_count][0] = message->data[0];
        message_heads[message_count][1] = message->data[1];
    }
    message_count++;
    return CANHUB_OK;
}

void CanHubSession_SetError(CanHubSession *session, const char *message)
{
}

}
