#include "platform/linux/libcanhub/canhub_uds.h"

#include <string.h>

#include "platform/linux/libcanhub/canhub_internal.h"

#define POSITIVE_OFFSET 0x40
#define NEGATIVE_RESPONSE 0x7F
#define RESPONSE_PENDING 0x78
#define TRANSFER_DATA 0x36
#define TRANSFER_HEADER_SIZE 2
#define SUPPRESS_POSITIVE_RESPONSE 0x80
#define NEGATIVE_CODE_OFFSET 2
#define NEGATIVE_ANSWER_SIZE 3
#define FIRST_BYTE_MASK 0xFFu
#define PENDING_MASK 0xFF00FFu
#define PENDING_VALUE 0x78007Fu
#define BITS_PER_BYTE 8
#define RESPONSE_ID_MASK (CANHUB_CAN_ID_MASK | CANHUB_CAN_ID_FLAG_EFF)

static const uint8_t SERVICES_WITH_SUB_FUNCTION[] = { 0x10, 0x11, 0x19, 0x27, 0x28, 0x29, 0x31, 0x3E, 0x83, 0x84, 0x85, 0x86, 0x87 };

static bool suppressesAnswer(const uint8_t *request, uint16_t length);
static void fillPattern(CanHubTxnPattern *pattern, uint32_t can_id, uint64_t payload_mask, uint64_t payload_value);
static uint64_t prefixMask(uint8_t length);
static uint64_t prefixValue(const uint8_t *bytes, uint8_t length);

/* ---------- public ---------- */

int32_t canhub_uds_begin(CanHubSession *session, const CanHubUds *uds, int32_t timeout_ms)
{
    CanHubTxnBegin begin;
    CanHubTxnTemplate definition;
    int32_t result;
    uint8_t i;

    if (uds == NULL || uds->struct_size != sizeof(*uds)) {
        CanHubSession_SetError(session, "uds needs struct_size set");
        return CANHUB_ERR_ARGUMENT;
    }

    memset(&begin, 0, sizeof(begin));
    begin.struct_size = sizeof(begin);
    begin.flags = (uint8_t)((uds->flags & CANHUB_TXN_FLAG_EXCLUSIVE) | CANHUB_TXN_FLAG_ISOTP);
    result = canhub_txn_begin(session, &begin, timeout_ms);
    if (result != CANHUB_OK) {
        return result;
    }

    for(i=0; i<=CANHUB_UDS_EXPECT_MAX; i++) {
        CanHubUds_Template(uds, i, &definition);
        result = canhub_txn_template(session, &definition, timeout_ms);
        if (result != CANHUB_OK) {
            return result;
        }
    }
    return CANHUB_OK;
}

int32_t canhub_uds_request(
    CanHubSession *session,
    const CanHubUds *uds,
    const uint8_t *request,
    uint16_t length,
    const uint8_t *expect,
    uint8_t expect_length,
    int32_t timeout_ms
)
{
    CanHubTxnMessage message;

    if (uds == NULL || uds->struct_size != sizeof(*uds) || !CanHubUds_Message(uds, request, length, expect, expect_length, &message)) {
        CanHubSession_SetError(session, "a request needs 1-4095 bytes, an expected answer of 1-8 bytes starting with its service id + 0x40");
        return CANHUB_ERR_ARGUMENT;
    }
    return canhub_txn_send_message(session, &message, timeout_ms);
}

int32_t canhub_uds_transfer(
    CanHubSession *session,
    const CanHubUds *uds,
    const uint8_t *data,
    uint32_t length,
    uint16_t block_size,
    int32_t timeout_ms
)
{
    uint8_t block[CANHUB_ISOTP_MESSAGE_MAX];
    uint8_t expect[TRANSFER_HEADER_SIZE];
    uint32_t offset = 0;
    uint16_t piece;
    uint8_t counter = 1;
    int32_t result;

    if (data == NULL || length == 0 || block_size == 0 || block_size > CANHUB_UDS_TRANSFER_BLOCK_MAX) {
        CanHubSession_SetError(session, "a transfer needs data and a block size of 1-4093 bytes");
        return CANHUB_ERR_ARGUMENT;
    }

    while (offset < length) {
        piece = (length - offset < block_size) ? (uint16_t)(length - offset) : block_size;
        block[0] = TRANSFER_DATA;
        block[1] = counter;
        memcpy(block + TRANSFER_HEADER_SIZE, data + offset, piece);
        expect[0] = TRANSFER_DATA + POSITIVE_OFFSET;
        expect[1] = counter;
        result = canhub_uds_request(session, uds, block, (uint16_t)(TRANSFER_HEADER_SIZE + piece), expect, TRANSFER_HEADER_SIZE, timeout_ms);
        if (result != CANHUB_OK) {
            return result;
        }
        offset += piece;
        counter++;
    }
    return CANHUB_OK;
}

uint8_t canhub_uds_negative_code(const CanHubTxnResult *result)
{
    if (result == NULL || !result->has_rx_frame || result->rx_frame.length < NEGATIVE_ANSWER_SIZE) {
        return 0;
    }
    if (result->rx_frame.payload[0] != NEGATIVE_RESPONSE) {
        return 0;
    }
    return result->rx_frame.payload[NEGATIVE_CODE_OFFSET];
}

void CanHubUds_Template(const CanHubUds *uds, uint8_t expect_length, CanHubTxnTemplate *definition)
{
    uint32_t response_id = uds->transport.response_id;

    memset(definition, 0, sizeof(*definition));
    definition->struct_size = sizeof(*definition);
    definition->template_id = expect_length;
    definition->type = CANHUB_TXN_TYPE_ISOTP;
    definition->isotp = uds->transport;
    definition->timeout_ms = uds->p2_ms;
    if (expect_length == CANHUB_UDS_NO_ANSWER_TEMPLATE) {
        definition->advance_mode = CANHUB_TXN_ADVANCE_NONE;
        return;
    }

    definition->advance_mode = CANHUB_TXN_ADVANCE_COUNT;
    definition->count_k = 1;
    definition->flags = CANHUB_TXN_TEMPLATE_MISMATCH_ABORT;
    definition->extend_timeout_ms = uds->p2_extended_ms;
    definition->patterns_present = (1u << CANHUB_TXN_PATTERN_ADVANCE) | (1u << CANHUB_TXN_PATTERN_ABORT) | (1u << CANHUB_TXN_PATTERN_EXTEND);
    fillPattern(&definition->patterns[CANHUB_TXN_PATTERN_ADVANCE], response_id, prefixMask(expect_length), 0);
    fillPattern(&definition->patterns[CANHUB_TXN_PATTERN_ABORT], response_id, FIRST_BYTE_MASK, NEGATIVE_RESPONSE);
    fillPattern(&definition->patterns[CANHUB_TXN_PATTERN_EXTEND], response_id, PENDING_MASK, PENDING_VALUE);
}

bool CanHubUds_Message(
    const CanHubUds *uds,
    const uint8_t *request,
    uint16_t length,
    const uint8_t *expect,
    uint8_t expect_length,
    CanHubTxnMessage *message
)
{
    uint8_t positive;

    if (request == NULL || length == 0 || length > CANHUB_ISOTP_MESSAGE_MAX) {
        return false;
    }
    positive = (uint8_t)(request[0] + POSITIVE_OFFSET);
    if (expect == NULL) {
        expect = &positive;
        expect_length = 1;
    }
    if (expect_length == 0 || expect_length > CANHUB_UDS_EXPECT_MAX || expect[0] != positive) {
        return false;
    }

    memset(message, 0, sizeof(*message));
    message->struct_size = sizeof(*message);
    message->can_id = uds->request_id;
    message->data = request;
    message->length = length;
    if (suppressesAnswer(request, length)) {
        message->template_id = CANHUB_UDS_NO_ANSWER_TEMPLATE;
        return true;
    }

    message->template_id = expect_length;
    message->flags = CANHUB_TXN_STEP_EXPECT_VALUE;
    message->expect_value = prefixValue(expect, expect_length);
    return true;
}

/* ---------- private ---------- */

static bool suppressesAnswer(const uint8_t *request, uint16_t length)
{
    uint8_t i;

    if (length < 2 || (request[1] & SUPPRESS_POSITIVE_RESPONSE) == 0) {
        return false;
    }
    for(i=0; i<sizeof(SERVICES_WITH_SUB_FUNCTION); i++) {
        if (SERVICES_WITH_SUB_FUNCTION[i] == request[0]) {
            return true;
        }
    }
    return false;
}

static void fillPattern(CanHubTxnPattern *pattern, uint32_t can_id, uint64_t payload_mask, uint64_t payload_value)
{
    pattern->can_id = can_id;
    pattern->can_mask = RESPONSE_ID_MASK;
    pattern->payload_mask = payload_mask;
    pattern->payload_value = payload_value;
}

static uint64_t prefixMask(uint8_t length)
{
    uint64_t mask = 0;
    uint8_t i;

    for(i=0; i<length; i++) {
        mask |= (uint64_t)FIRST_BYTE_MASK << (i * BITS_PER_BYTE);
    }
    return mask;
}

static uint64_t prefixValue(const uint8_t *bytes, uint8_t length)
{
    uint64_t value = 0;
    uint8_t i;

    for(i=0; i<length; i++) {
        value |= (uint64_t)bytes[i] << (i * BITS_PER_BYTE);
    }
    return value;
}
