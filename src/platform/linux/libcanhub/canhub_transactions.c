#include "platform/linux/libcanhub/canhub_transactions.h"

#include <string.h>

static void isotpToWire(const CanHubTxnIsotp *isotp, TxnIsotpParameters *parameters);
static uint32_t recordBytes(const TxnStep *records, uint8_t count);

/* ---------- public ---------- */

bool CanHubTransactions_BeginToWire(const CanHubTxnBegin *begin, TxnBeginMessage *message)
{
    if (begin->struct_size != sizeof(*begin) || begin->roster_count > CANHUB_TXN_ROSTER_MAX) {
        return false;
    }

    memset(message, 0, sizeof(*message));
    message->flags = begin->flags;
    message->origin.source = begin->origin_source;
    message->origin.shift = begin->origin_shift;
    message->origin.payload_offset = begin->origin_payload_offset;
    message->origin.mask = begin->origin_mask;
    message->roster_count = (uint8_t)begin->roster_count;
    memcpy(message->roster, begin->roster, (size_t)begin->roster_count * sizeof(begin->roster[0]));

    return true;
}

bool CanHubTransactions_TemplateToWire(const CanHubTxnTemplate *template_definition, TxnTemplateMessage *message)
{
    uint8_t i;

    if (template_definition->struct_size != sizeof(*template_definition)) {
        return false;
    }
    if (template_definition->template_id >= CANHUB_TXN_TEMPLATES_MAX) {
        return false;
    }

    memset(message, 0, sizeof(*message));
    message->template_id = template_definition->template_id;
    message->advance_mode = template_definition->advance_mode;
    message->flags = template_definition->flags;
    message->count_k = template_definition->count_k;
    message->patterns_present = template_definition->patterns_present;
    message->timeout_ms = template_definition->timeout_ms;
    message->gap_us = template_definition->gap_us;
    message->extend_timeout_ms = template_definition->extend_timeout_ms;
    message->type = template_definition->type;
    isotpToWire(&template_definition->isotp, &message->isotp);
    for(i=0; i<CANHUB_TXN_PATTERNS; i++) {
        message->patterns[i].can_id = template_definition->patterns[i].can_id;
        message->patterns[i].can_mask = template_definition->patterns[i].can_mask;
        message->patterns[i].payload_mask = template_definition->patterns[i].payload_mask;
        message->patterns[i].payload_value = template_definition->patterns[i].payload_value;
    }

    return true;
}

void CanHubTransactions_StepToWire(const CanHubTxnStep *step, TxnStep *wire_step)
{
    memset(wire_step, 0, sizeof(*wire_step));
    wire_step->template_id = step->template_id;
    wire_step->flags = step->flags;
    wire_step->frame_flags = step->frame_flags & (CANHUB_FRAME_FLAG_FD | CANHUB_FRAME_FLAG_BRS);
    wire_step->can_id = step->can_id;
    wire_step->expect_value = step->expect_value;
    wire_step->payload_length = step->length;
    if (step->length <= CANHUB_FRAME_PAYLOAD_MAX) {
        memcpy(wire_step->payload, step->payload, step->length);
    }
}

uint8_t CanHubTransactions_MessageToWire(const CanHubTxnMessage *message, TxnStep *records)
{
    uint16_t offset = 0;
    uint16_t piece;
    uint8_t count = 0;

    if (message->struct_size != sizeof(*message) || message->data == NULL || message->length == 0) {
        return 0;
    }
    if (message->length > CANHUB_ISOTP_MESSAGE_MAX || message->template_id >= CANHUB_TXN_TEMPLATES_MAX) {
        return 0;
    }
    if ((message->flags & ~CANHUB_TXN_STEP_EXPECT_VALUE) != 0) {
        return 0;
    }

    while (offset < message->length) {
        piece = (uint16_t)(message->length - offset);
        if (piece > CANHUB_TRANSACTIONS_MESSAGE_RECORD_BYTES) {
            piece = CANHUB_TRANSACTIONS_MESSAGE_RECORD_BYTES;
        }
        memset(&records[count], 0, sizeof(records[count]));
        records[count].template_id = message->template_id;
        records[count].can_id = message->can_id;
        records[count].flags = TXN_STEP_FLAG_MESSAGE | TXN_STEP_FLAG_CONTINUED;
        records[count].payload_length = (uint8_t)piece;
        memcpy(records[count].payload, message->data + offset, piece);
        offset = (uint16_t)(offset + piece);
        count++;
    }

    records[count - 1].flags = (uint8_t)(TXN_STEP_FLAG_MESSAGE | message->flags);
    records[count - 1].expect_value = message->expect_value;

    return count;
}

bool CanHubTransactions_HasCreditFor(const TransactionSender *sender, const TxnStep *records, uint8_t count)
{
    uint32_t held_bytes = sender->has_held ? (uint32_t)TxnStep_RecordSize(&sender->held) : 0;

    return TransactionSender_BytesInFlight(sender) + held_bytes + recordBytes(records, count) <= sender->window_bytes;
}

void CanHubTransactions_ResultFromWire(const TxnStatusMessage *status, CanHubTxnResult *result)
{
    memset(result, 0, sizeof(*result));
    result->struct_size = sizeof(*result);
    result->state = status->state;
    result->reason = status->reason;
    result->has_rx_frame = (status->flags & TXN_STATUS_FLAG_HAS_RX_FRAME) != 0;
    result->failing_seq = status->failing_seq;
    result->completed_steps = status->completed_steps;
    result->offending_origin = status->offending_origin;
    result->seen_bitmap = status->seen_bitmap;
    result->expected = status->expected;
    result->got = status->got;
    result->rx_frame.can_id = status->rx_frame.can_id;
    result->rx_frame.flags = status->rx_frame.frame_flags;
    result->rx_frame.length = status->rx_frame.payload_length;
    memcpy(result->rx_frame.payload, status->rx_frame.payload, status->rx_frame.payload_length);
}

int32_t CanHubTransactions_BeginResult(uint8_t begin_status)
{
    if (begin_status == kTXN_BEGIN_STATUS_OK) {
        return CANHUB_OK;
    }
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

const char *CanHubTransactions_BeginDetail(uint8_t begin_status)
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

/* ---------- private ---------- */

static void isotpToWire(const CanHubTxnIsotp *isotp, TxnIsotpParameters *parameters)
{
    parameters->response_id = isotp->response_id;
    parameters->flags = isotp->flags;
    parameters->tx_address = isotp->tx_address;
    parameters->rx_address = isotp->rx_address;
    parameters->padding = isotp->padding;
    parameters->tx_data_length = isotp->tx_data_length;
    parameters->block_size = isotp->block_size;
    parameters->st_min = isotp->st_min;
}

static uint32_t recordBytes(const TxnStep *records, uint8_t count)
{
    uint32_t bytes = 0;
    uint8_t i;

    for(i=0; i<count; i++) {
        bytes += (uint32_t)TxnStep_RecordSize(&records[i]);
    }
    return bytes;
}
