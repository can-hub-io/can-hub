#include "protocol/txn_message.h"

#include <string.h>

#include "protocol/message_header.h"
#include "protocol/wire.h"

#define BEGIN_CHANNEL_OFFSET 0
#define BEGIN_FLAGS_OFFSET 1
#define BEGIN_ORIGIN_SOURCE_OFFSET 2
#define BEGIN_ORIGIN_SHIFT_OFFSET 3
#define BEGIN_ORIGIN_MASK_OFFSET 4
#define BEGIN_ORIGIN_PAYLOAD_OFFSET_OFFSET 8
#define BEGIN_ROSTER_COUNT_OFFSET 9
#define BEGIN_ROSTER_OFFSET 12
#define ROSTER_ENTRY_SIZE 4

#define BEGIN_ACK_CHANNEL_OFFSET 0
#define BEGIN_ACK_STATUS_OFFSET 1
#define BEGIN_ACK_TXN_ID_OFFSET 2
#define BEGIN_ACK_WINDOW_BYTES_OFFSET 4

#define TEMPLATE_CHANNEL_OFFSET 0
#define TEMPLATE_TEMPLATE_ID_OFFSET 1
#define TEMPLATE_TXN_ID_OFFSET 2
#define TEMPLATE_ADVANCE_MODE_OFFSET 4
#define TEMPLATE_FLAGS_OFFSET 5
#define TEMPLATE_COUNT_K_OFFSET 6
#define TEMPLATE_PATTERNS_PRESENT_OFFSET 7
#define TEMPLATE_TIMEOUT_MS_OFFSET 8
#define TEMPLATE_GAP_US_OFFSET 12
#define TEMPLATE_TYPE_OFFSET 16
#define TEMPLATE_PATTERNS_OFFSET 20
#define PATTERN_SIZE 24
#define PATTERN_CAN_ID_OFFSET 0
#define PATTERN_CAN_MASK_OFFSET 4
#define PATTERN_PAYLOAD_MASK_OFFSET 8
#define PATTERN_PAYLOAD_VALUE_OFFSET 16
#define TEMPLATE_ISOTP_RESPONSE_ID_OFFSET 92
#define TEMPLATE_ISOTP_FLAGS_OFFSET 96
#define TEMPLATE_ISOTP_TX_ADDRESS_OFFSET 97
#define TEMPLATE_ISOTP_RX_ADDRESS_OFFSET 98
#define TEMPLATE_ISOTP_PADDING_OFFSET 99
#define TEMPLATE_ISOTP_TX_DATA_LENGTH_OFFSET 100
#define TEMPLATE_ISOTP_BLOCK_SIZE_OFFSET 101
#define TEMPLATE_ISOTP_ST_MIN_OFFSET 102

#define PROGRESS_CHANNEL_OFFSET 0
#define PROGRESS_TXN_ID_OFFSET 2
#define PROGRESS_COMPLETED_STEPS_OFFSET 4
#define PROGRESS_CONSUMED_BYTES_OFFSET 8

#define STATUS_CHANNEL_OFFSET 0
#define STATUS_STATE_OFFSET 1
#define STATUS_TXN_ID_OFFSET 2
#define STATUS_FAILING_SEQ_OFFSET 4
#define STATUS_OFFENDING_ORIGIN_OFFSET 8
#define STATUS_SEEN_BITMAP_OFFSET 12
#define STATUS_EXPECTED_OFFSET 20
#define STATUS_GOT_OFFSET 28
#define STATUS_COMPLETED_STEPS_OFFSET 36
#define STATUS_RX_CAN_ID_OFFSET 40
#define STATUS_REASON_OFFSET 44
#define STATUS_RX_PAYLOAD_LENGTH_OFFSET 45
#define STATUS_RX_FRAME_FLAGS_OFFSET 46
#define STATUS_FLAGS_OFFSET 47
#define STATUS_RX_PAYLOAD_OFFSET 48

#define ABORT_CHANNEL_OFFSET 0
#define ABORT_TXN_ID_OFFSET 2

static size_t encodeFixedBody(uint8_t type, size_t body_size, uint8_t *buffer, size_t buffer_size, uint8_t **body);
static void writePattern(uint8_t *destination, const TxnPattern *pattern);
static void readPattern(const uint8_t *source, TxnPattern *pattern);
static void writeIsotpParameters(uint8_t *body, const TxnIsotpParameters *isotp);
static void readIsotpParameters(const uint8_t *body, TxnIsotpParameters *isotp);

/* ---------- public ---------- */

void TxnMessage_SetChannel(uint8_t *encoded, uint8_t channel)
{
    encoded[MESSAGE_HEADER_SIZE + TXN_CHANNEL_OFFSET] = channel;
}

uint8_t TxnMessage_Channel(const uint8_t *payload)
{
    return payload[TXN_CHANNEL_OFFSET];
}

size_t TxnBeginMessage_Encode(const TxnBeginMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;
    size_t total_size;
    uint8_t i;

    if (self->roster_count > TXN_ROSTER_MAX) {
        return 0;
    }
    total_size = encodeFixedBody(kMESSAGE_TYPE_TXN_BEGIN, TXN_BEGIN_BODY_SIZE, buffer, buffer_size, &body);
    if (total_size == 0) {
        return 0;
    }

    body[BEGIN_CHANNEL_OFFSET] = self->channel;
    body[BEGIN_FLAGS_OFFSET] = self->flags;
    body[BEGIN_ORIGIN_SOURCE_OFFSET] = self->origin.source;
    body[BEGIN_ORIGIN_SHIFT_OFFSET] = self->origin.shift;
    Wire_WriteU32(body + BEGIN_ORIGIN_MASK_OFFSET, self->origin.mask);
    body[BEGIN_ORIGIN_PAYLOAD_OFFSET_OFFSET] = self->origin.payload_offset;
    body[BEGIN_ROSTER_COUNT_OFFSET] = self->roster_count;
    for(i=0; i<self->roster_count; i++) {
        Wire_WriteU32(body + BEGIN_ROSTER_OFFSET + i * ROSTER_ENTRY_SIZE, self->roster[i]);
    }

    return total_size;
}

bool TxnBeginMessage_Decode(TxnBeginMessage *self, const uint8_t *payload, size_t payload_length)
{
    uint8_t i;

    if (payload_length < TXN_BEGIN_BODY_SIZE) {
        return false;
    }
    if (payload[BEGIN_ROSTER_COUNT_OFFSET] > TXN_ROSTER_MAX) {
        return false;
    }

    memset(self, 0, sizeof(*self));
    self->channel = payload[BEGIN_CHANNEL_OFFSET];
    self->flags = payload[BEGIN_FLAGS_OFFSET];
    self->origin.source = payload[BEGIN_ORIGIN_SOURCE_OFFSET];
    self->origin.shift = payload[BEGIN_ORIGIN_SHIFT_OFFSET];
    self->origin.mask = Wire_ReadU32(payload + BEGIN_ORIGIN_MASK_OFFSET);
    self->origin.payload_offset = payload[BEGIN_ORIGIN_PAYLOAD_OFFSET_OFFSET];
    self->roster_count = payload[BEGIN_ROSTER_COUNT_OFFSET];
    for(i=0; i<self->roster_count; i++) {
        self->roster[i] = Wire_ReadU32(payload + BEGIN_ROSTER_OFFSET + i * ROSTER_ENTRY_SIZE);
    }

    return true;
}

size_t TxnBeginAckMessage_Encode(const TxnBeginAckMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;
    size_t total_size;

    total_size = encodeFixedBody(kMESSAGE_TYPE_TXN_BEGIN_ACK, TXN_BEGIN_ACK_BODY_SIZE, buffer, buffer_size, &body);
    if (total_size == 0) {
        return 0;
    }

    body[BEGIN_ACK_CHANNEL_OFFSET] = self->channel;
    body[BEGIN_ACK_STATUS_OFFSET] = self->status;
    Wire_WriteU16(body + BEGIN_ACK_TXN_ID_OFFSET, self->txn_id);
    Wire_WriteU32(body + BEGIN_ACK_WINDOW_BYTES_OFFSET, self->window_bytes);

    return total_size;
}

bool TxnBeginAckMessage_Decode(TxnBeginAckMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < TXN_BEGIN_ACK_BODY_SIZE) {
        return false;
    }

    self->channel = payload[BEGIN_ACK_CHANNEL_OFFSET];
    self->status = payload[BEGIN_ACK_STATUS_OFFSET];
    self->txn_id = Wire_ReadU16(payload + BEGIN_ACK_TXN_ID_OFFSET);
    self->window_bytes = Wire_ReadU32(payload + BEGIN_ACK_WINDOW_BYTES_OFFSET);

    return true;
}

size_t TxnTemplateMessage_Encode(const TxnTemplateMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;
    size_t total_size;
    uint8_t i;

    if (self->template_id >= TXN_TEMPLATES_MAX) {
        return 0;
    }
    total_size = encodeFixedBody(kMESSAGE_TYPE_TXN_TEMPLATE, TXN_TEMPLATE_BODY_SIZE, buffer, buffer_size, &body);
    if (total_size == 0) {
        return 0;
    }

    body[TEMPLATE_CHANNEL_OFFSET] = self->channel;
    body[TEMPLATE_TEMPLATE_ID_OFFSET] = self->template_id;
    Wire_WriteU16(body + TEMPLATE_TXN_ID_OFFSET, self->txn_id);
    body[TEMPLATE_ADVANCE_MODE_OFFSET] = self->advance_mode;
    body[TEMPLATE_FLAGS_OFFSET] = self->flags;
    body[TEMPLATE_COUNT_K_OFFSET] = self->count_k;
    body[TEMPLATE_PATTERNS_PRESENT_OFFSET] = self->patterns_present;
    Wire_WriteU32(body + TEMPLATE_TIMEOUT_MS_OFFSET, self->timeout_ms);
    Wire_WriteU32(body + TEMPLATE_GAP_US_OFFSET, self->gap_us);
    body[TEMPLATE_TYPE_OFFSET] = self->type;
    for(i=0; i<kTXN_PATTERN_MAX; i++) {
        writePattern(body + TEMPLATE_PATTERNS_OFFSET + i * PATTERN_SIZE, &self->patterns[i]);
    }
    writeIsotpParameters(body, &self->isotp);

    return total_size;
}

bool TxnTemplateMessage_Decode(TxnTemplateMessage *self, const uint8_t *payload, size_t payload_length)
{
    uint8_t i;

    if (payload_length < TXN_TEMPLATE_BODY_SIZE) {
        return false;
    }
    if (payload[TEMPLATE_TEMPLATE_ID_OFFSET] >= TXN_TEMPLATES_MAX) {
        return false;
    }

    self->channel = payload[TEMPLATE_CHANNEL_OFFSET];
    self->template_id = payload[TEMPLATE_TEMPLATE_ID_OFFSET];
    self->txn_id = Wire_ReadU16(payload + TEMPLATE_TXN_ID_OFFSET);
    self->advance_mode = payload[TEMPLATE_ADVANCE_MODE_OFFSET];
    self->flags = payload[TEMPLATE_FLAGS_OFFSET];
    self->count_k = payload[TEMPLATE_COUNT_K_OFFSET];
    self->patterns_present = payload[TEMPLATE_PATTERNS_PRESENT_OFFSET];
    self->timeout_ms = Wire_ReadU32(payload + TEMPLATE_TIMEOUT_MS_OFFSET);
    self->gap_us = Wire_ReadU32(payload + TEMPLATE_GAP_US_OFFSET);
    self->type = payload[TEMPLATE_TYPE_OFFSET];
    for(i=0; i<kTXN_PATTERN_MAX; i++) {
        readPattern(payload + TEMPLATE_PATTERNS_OFFSET + i * PATTERN_SIZE, &self->patterns[i]);
    }
    readIsotpParameters(payload, &self->isotp);

    return true;
}

size_t TxnProgressMessage_Encode(const TxnProgressMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;
    size_t total_size;

    total_size = encodeFixedBody(kMESSAGE_TYPE_TXN_PROGRESS, TXN_PROGRESS_BODY_SIZE, buffer, buffer_size, &body);
    if (total_size == 0) {
        return 0;
    }

    body[PROGRESS_CHANNEL_OFFSET] = self->channel;
    Wire_WriteU16(body + PROGRESS_TXN_ID_OFFSET, self->txn_id);
    Wire_WriteU32(body + PROGRESS_COMPLETED_STEPS_OFFSET, self->completed_steps);
    Wire_WriteU32(body + PROGRESS_CONSUMED_BYTES_OFFSET, self->consumed_bytes);

    return total_size;
}

bool TxnProgressMessage_Decode(TxnProgressMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < TXN_PROGRESS_BODY_SIZE) {
        return false;
    }

    self->channel = payload[PROGRESS_CHANNEL_OFFSET];
    self->txn_id = Wire_ReadU16(payload + PROGRESS_TXN_ID_OFFSET);
    self->completed_steps = Wire_ReadU32(payload + PROGRESS_COMPLETED_STEPS_OFFSET);
    self->consumed_bytes = Wire_ReadU32(payload + PROGRESS_CONSUMED_BYTES_OFFSET);

    return true;
}

size_t TxnStatusMessage_Encode(const TxnStatusMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;
    size_t total_size;

    if (!FrameMessage_IsPayloadLengthValid(self->rx_frame.payload_length, self->rx_frame.frame_flags)) {
        return 0;
    }
    total_size = encodeFixedBody(kMESSAGE_TYPE_TXN_STATUS, TXN_STATUS_BODY_SIZE, buffer, buffer_size, &body);
    if (total_size == 0) {
        return 0;
    }

    body[STATUS_CHANNEL_OFFSET] = self->channel;
    body[STATUS_STATE_OFFSET] = self->state;
    Wire_WriteU16(body + STATUS_TXN_ID_OFFSET, self->txn_id);
    Wire_WriteU32(body + STATUS_FAILING_SEQ_OFFSET, self->failing_seq);
    Wire_WriteU32(body + STATUS_OFFENDING_ORIGIN_OFFSET, self->offending_origin);
    Wire_WriteU64(body + STATUS_SEEN_BITMAP_OFFSET, self->seen_bitmap);
    Wire_WriteU64(body + STATUS_EXPECTED_OFFSET, self->expected);
    Wire_WriteU64(body + STATUS_GOT_OFFSET, self->got);
    Wire_WriteU32(body + STATUS_COMPLETED_STEPS_OFFSET, self->completed_steps);
    Wire_WriteU32(body + STATUS_RX_CAN_ID_OFFSET, self->rx_frame.can_id);
    body[STATUS_REASON_OFFSET] = self->reason;
    body[STATUS_RX_PAYLOAD_LENGTH_OFFSET] = self->rx_frame.payload_length;
    body[STATUS_RX_FRAME_FLAGS_OFFSET] = self->rx_frame.frame_flags;
    body[STATUS_FLAGS_OFFSET] = self->flags;
    memcpy(body + STATUS_RX_PAYLOAD_OFFSET, self->rx_frame.payload, self->rx_frame.payload_length);

    return total_size;
}

bool TxnStatusMessage_Decode(TxnStatusMessage *self, const uint8_t *payload, size_t payload_length)
{
    uint8_t rx_payload_length;
    uint8_t rx_frame_flags;

    if (payload_length < TXN_STATUS_BODY_SIZE) {
        return false;
    }
    rx_payload_length = payload[STATUS_RX_PAYLOAD_LENGTH_OFFSET];
    rx_frame_flags = payload[STATUS_RX_FRAME_FLAGS_OFFSET];
    if (!FrameMessage_IsPayloadLengthValid(rx_payload_length, rx_frame_flags)) {
        return false;
    }

    memset(self, 0, sizeof(*self));
    self->channel = payload[STATUS_CHANNEL_OFFSET];
    self->state = payload[STATUS_STATE_OFFSET];
    self->txn_id = Wire_ReadU16(payload + STATUS_TXN_ID_OFFSET);
    self->failing_seq = Wire_ReadU32(payload + STATUS_FAILING_SEQ_OFFSET);
    self->offending_origin = Wire_ReadU32(payload + STATUS_OFFENDING_ORIGIN_OFFSET);
    self->seen_bitmap = Wire_ReadU64(payload + STATUS_SEEN_BITMAP_OFFSET);
    self->expected = Wire_ReadU64(payload + STATUS_EXPECTED_OFFSET);
    self->got = Wire_ReadU64(payload + STATUS_GOT_OFFSET);
    self->completed_steps = Wire_ReadU32(payload + STATUS_COMPLETED_STEPS_OFFSET);
    self->reason = payload[STATUS_REASON_OFFSET];
    self->flags = payload[STATUS_FLAGS_OFFSET];
    self->rx_frame.can_id = Wire_ReadU32(payload + STATUS_RX_CAN_ID_OFFSET);
    self->rx_frame.payload_length = rx_payload_length;
    self->rx_frame.frame_flags = rx_frame_flags;
    memcpy(self->rx_frame.payload, payload + STATUS_RX_PAYLOAD_OFFSET, rx_payload_length);

    return true;
}

size_t TxnAbortMessage_Encode(const TxnAbortMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;
    size_t total_size;

    total_size = encodeFixedBody(kMESSAGE_TYPE_TXN_ABORT, TXN_ABORT_BODY_SIZE, buffer, buffer_size, &body);
    if (total_size == 0) {
        return 0;
    }

    body[ABORT_CHANNEL_OFFSET] = self->channel;
    Wire_WriteU16(body + ABORT_TXN_ID_OFFSET, self->txn_id);

    return total_size;
}

bool TxnAbortMessage_Decode(TxnAbortMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < TXN_ABORT_BODY_SIZE) {
        return false;
    }

    self->channel = payload[ABORT_CHANNEL_OFFSET];
    self->txn_id = Wire_ReadU16(payload + ABORT_TXN_ID_OFFSET);

    return true;
}

/* ---------- private ---------- */

static size_t encodeFixedBody(uint8_t type, size_t body_size, uint8_t *buffer, size_t buffer_size, uint8_t **body)
{
    MessageHeader header;
    size_t total_size = MESSAGE_HEADER_SIZE + body_size;

    if (buffer_size < total_size) {
        return 0;
    }

    header.type = type;
    header.flags = 0;
    header.length = (uint16_t)body_size;
    MessageHeader_Encode(&header, buffer, buffer_size);

    *body = buffer + MESSAGE_HEADER_SIZE;
    memset(*body, 0, body_size);

    return total_size;
}

static void writePattern(uint8_t *destination, const TxnPattern *pattern)
{
    Wire_WriteU32(destination + PATTERN_CAN_ID_OFFSET, pattern->can_id);
    Wire_WriteU32(destination + PATTERN_CAN_MASK_OFFSET, pattern->can_mask);
    Wire_WriteU64(destination + PATTERN_PAYLOAD_MASK_OFFSET, pattern->payload_mask);
    Wire_WriteU64(destination + PATTERN_PAYLOAD_VALUE_OFFSET, pattern->payload_value);
}

static void readPattern(const uint8_t *source, TxnPattern *pattern)
{
    pattern->can_id = Wire_ReadU32(source + PATTERN_CAN_ID_OFFSET);
    pattern->can_mask = Wire_ReadU32(source + PATTERN_CAN_MASK_OFFSET);
    pattern->payload_mask = Wire_ReadU64(source + PATTERN_PAYLOAD_MASK_OFFSET);
    pattern->payload_value = Wire_ReadU64(source + PATTERN_PAYLOAD_VALUE_OFFSET);
}

static void writeIsotpParameters(uint8_t *body, const TxnIsotpParameters *isotp)
{
    Wire_WriteU32(body + TEMPLATE_ISOTP_RESPONSE_ID_OFFSET, isotp->response_id);
    body[TEMPLATE_ISOTP_FLAGS_OFFSET] = isotp->flags;
    body[TEMPLATE_ISOTP_TX_ADDRESS_OFFSET] = isotp->tx_address;
    body[TEMPLATE_ISOTP_RX_ADDRESS_OFFSET] = isotp->rx_address;
    body[TEMPLATE_ISOTP_PADDING_OFFSET] = isotp->padding;
    body[TEMPLATE_ISOTP_TX_DATA_LENGTH_OFFSET] = isotp->tx_data_length;
    body[TEMPLATE_ISOTP_BLOCK_SIZE_OFFSET] = isotp->block_size;
    body[TEMPLATE_ISOTP_ST_MIN_OFFSET] = isotp->st_min;
}

static void readIsotpParameters(const uint8_t *body, TxnIsotpParameters *isotp)
{
    isotp->response_id = Wire_ReadU32(body + TEMPLATE_ISOTP_RESPONSE_ID_OFFSET);
    isotp->flags = body[TEMPLATE_ISOTP_FLAGS_OFFSET];
    isotp->tx_address = body[TEMPLATE_ISOTP_TX_ADDRESS_OFFSET];
    isotp->rx_address = body[TEMPLATE_ISOTP_RX_ADDRESS_OFFSET];
    isotp->padding = body[TEMPLATE_ISOTP_PADDING_OFFSET];
    isotp->tx_data_length = body[TEMPLATE_ISOTP_TX_DATA_LENGTH_OFFSET];
    isotp->block_size = body[TEMPLATE_ISOTP_BLOCK_SIZE_OFFSET];
    isotp->st_min = body[TEMPLATE_ISOTP_ST_MIN_OFFSET];
}
