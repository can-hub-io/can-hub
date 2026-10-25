#include "protocol/txn_steps_message.h"

#include <string.h>

#include "protocol/message_header.h"
#include "protocol/txn_message.h"
#include "protocol/wire.h"

#define STEPS_CHANNEL_OFFSET 0
#define STEPS_STEP_COUNT_OFFSET 1
#define STEPS_TXN_ID_OFFSET 2
#define STEPS_FIRST_SEQ_OFFSET 4
#define STEPS_FLAGS_OFFSET 8
#define STEPS_RECORDS_OFFSET TXN_STEPS_FIXED_FIELDS_SIZE

#define STEP_TEMPLATE_ID_OFFSET 0
#define STEP_FLAGS_OFFSET 1
#define STEP_PAYLOAD_LENGTH_OFFSET 2
#define STEP_FRAME_FLAGS_OFFSET 3
#define STEP_CAN_ID_OFFSET 4
#define STEP_PAYLOAD_OFFSET TXN_STEP_RECORD_HEADER_SIZE

#define STEPS_COUNT_MAX UINT8_MAX
#define STEPS_FIRST_RECORD_OFFSET (MESSAGE_HEADER_SIZE + STEPS_RECORDS_OFFSET)

static bool isStepValid(const TxnStep *step);
static bool isMessageRecordValid(const TxnStep *step);
static size_t recordSize(uint8_t payload_length, uint8_t flags);
static size_t paddedPayloadSize(uint8_t payload_length);
static size_t writerRoom(const TxnStepsWriter *self);
static bool areRecordsWellFormed(const uint8_t *records, uint16_t records_size, uint8_t step_count);

/* ---------- public ---------- */

size_t TxnStep_RecordSize(const TxnStep *self)
{
    return recordSize(self->payload_length, self->flags);
}

size_t TxnStep_Encode(const TxnStep *self, uint8_t *record, size_t available)
{
    size_t size = TxnStep_RecordSize(self);

    if (!isStepValid(self)) {
        return 0;
    }
    if (available < size) {
        return 0;
    }

    memset(record, 0, size);
    record[STEP_TEMPLATE_ID_OFFSET] = self->template_id;
    record[STEP_FLAGS_OFFSET] = self->flags;
    record[STEP_PAYLOAD_LENGTH_OFFSET] = self->payload_length;
    record[STEP_FRAME_FLAGS_OFFSET] = self->frame_flags;
    Wire_WriteU32(record + STEP_CAN_ID_OFFSET, self->can_id);
    memcpy(record + STEP_PAYLOAD_OFFSET, self->payload, self->payload_length);
    if (self->flags & TXN_STEP_FLAG_EXPECT_VALUE) {
        Wire_WriteU64(record + STEP_PAYLOAD_OFFSET + paddedPayloadSize(self->payload_length), self->expect_value);
    }

    return size;
}

size_t TxnStep_Decode(TxnStep *self, const uint8_t *record, size_t available)
{
    size_t size;

    if (available < TXN_STEP_RECORD_HEADER_SIZE) {
        return 0;
    }

    memset(self, 0, sizeof(*self));
    self->template_id = record[STEP_TEMPLATE_ID_OFFSET];
    self->flags = record[STEP_FLAGS_OFFSET];
    self->payload_length = record[STEP_PAYLOAD_LENGTH_OFFSET];
    self->frame_flags = record[STEP_FRAME_FLAGS_OFFSET];
    self->can_id = Wire_ReadU32(record + STEP_CAN_ID_OFFSET);

    if (!isStepValid(self)) {
        return 0;
    }
    size = TxnStep_RecordSize(self);
    if (available < size) {
        return 0;
    }

    memcpy(self->payload, record + STEP_PAYLOAD_OFFSET, self->payload_length);
    if (self->flags & TXN_STEP_FLAG_EXPECT_VALUE) {
        self->expect_value = Wire_ReadU64(record + STEP_PAYLOAD_OFFSET + paddedPayloadSize(self->payload_length));
    }

    return size;
}

bool TxnStepsWriter_Init(TxnStepsWriter *self, const TxnStepsMessage *message, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;

    if (buffer_size < STEPS_FIRST_RECORD_OFFSET) {
        return false;
    }

    self->buffer = buffer;
    self->buffer_size = buffer_size;
    self->records_size = 0;
    self->step_count = 0;

    body = buffer + MESSAGE_HEADER_SIZE;
    memset(body, 0, TXN_STEPS_FIXED_FIELDS_SIZE);
    body[STEPS_CHANNEL_OFFSET] = message->channel;
    Wire_WriteU16(body + STEPS_TXN_ID_OFFSET, message->txn_id);
    Wire_WriteU32(body + STEPS_FIRST_SEQ_OFFSET, message->first_seq);
    body[STEPS_FLAGS_OFFSET] = message->flags;

    return true;
}

bool TxnStepsWriter_Append(TxnStepsWriter *self, const TxnStep *step)
{
    uint8_t *record = self->buffer + STEPS_FIRST_RECORD_OFFSET + self->records_size;
    size_t size;

    if (self->step_count == STEPS_COUNT_MAX) {
        return false;
    }
    size = TxnStep_Encode(step, record, writerRoom(self));
    if (size == 0) {
        return false;
    }

    self->records_size = (uint16_t)(self->records_size + size);
    self->step_count++;

    return true;
}

void TxnStepsWriter_MarkEnd(TxnStepsWriter *self)
{
    self->buffer[MESSAGE_HEADER_SIZE + STEPS_FLAGS_OFFSET] |= TXN_STEPS_FLAG_END;
}

size_t TxnStepsWriter_Finish(TxnStepsWriter *self)
{
    MessageHeader header;
    uint16_t body_size = (uint16_t)(TXN_STEPS_FIXED_FIELDS_SIZE + self->records_size);

    header.type = kMESSAGE_TYPE_TXN_STEPS;
    header.flags = 0;
    header.length = body_size;
    MessageHeader_Encode(&header, self->buffer, self->buffer_size);
    self->buffer[MESSAGE_HEADER_SIZE + STEPS_STEP_COUNT_OFFSET] = self->step_count;

    return (size_t)MESSAGE_HEADER_SIZE + body_size;
}

bool TxnStepsMessage_Decode(TxnStepsMessage *self, const uint8_t *payload, size_t payload_length)
{
    size_t records_size;
    uint8_t step_count;

    if (payload_length < TXN_STEPS_FIXED_FIELDS_SIZE) {
        return false;
    }
    records_size = payload_length - TXN_STEPS_FIXED_FIELDS_SIZE;
    if (records_size > TXN_STEPS_RECORD_BYTES_MAX) {
        return false;
    }
    step_count = payload[STEPS_STEP_COUNT_OFFSET];
    if (!areRecordsWellFormed(payload + STEPS_RECORDS_OFFSET, (uint16_t)records_size, step_count)) {
        return false;
    }

    self->channel = payload[STEPS_CHANNEL_OFFSET];
    self->step_count = step_count;
    self->txn_id = Wire_ReadU16(payload + STEPS_TXN_ID_OFFSET);
    self->first_seq = Wire_ReadU32(payload + STEPS_FIRST_SEQ_OFFSET);
    self->flags = payload[STEPS_FLAGS_OFFSET];
    self->records = payload + STEPS_RECORDS_OFFSET;
    self->records_size = (uint16_t)records_size;

    return true;
}

/* ---------- private ---------- */

static bool isStepValid(const TxnStep *step)
{
    if (step->template_id >= TXN_TEMPLATES_MAX) {
        return false;
    }
    if (step->flags & TXN_STEP_FLAG_MESSAGE) {
        return isMessageRecordValid(step);
    }
    if (step->flags & TXN_STEP_FLAG_CONTINUED) {
        return false;
    }
    if ((step->flags & TXN_STEP_FLAG_EXPECT_ONLY) && step->payload_length != 0) {
        return false;
    }
    return FrameMessage_IsPayloadLengthValid(step->payload_length, step->frame_flags);
}

static bool isMessageRecordValid(const TxnStep *step)
{
    if (step->payload_length == 0 || step->frame_flags != 0) {
        return false;
    }
    if (step->flags & TXN_STEP_FLAG_EXPECT_ONLY) {
        return false;
    }
    if (step->flags & TXN_STEP_FLAG_CONTINUED) {
        return (step->flags & TXN_STEP_FLAG_EXPECT_VALUE) == 0;
    }
    return true;
}

static size_t recordSize(uint8_t payload_length, uint8_t flags)
{
    size_t size = TXN_STEP_RECORD_HEADER_SIZE + paddedPayloadSize(payload_length);

    if (flags & TXN_STEP_FLAG_EXPECT_VALUE) {
        size += TXN_STEP_EXPECT_VALUE_SIZE;
    }
    return size;
}

static size_t paddedPayloadSize(uint8_t payload_length)
{
    return ((size_t)payload_length + TXN_STEP_RECORD_ALIGNMENT - 1) / TXN_STEP_RECORD_ALIGNMENT * TXN_STEP_RECORD_ALIGNMENT;
}

static size_t writerRoom(const TxnStepsWriter *self)
{
    size_t buffer_room = self->buffer_size - STEPS_FIRST_RECORD_OFFSET - self->records_size;
    size_t record_room = TXN_STEPS_RECORD_BYTES_MAX - self->records_size;

    if (buffer_room < record_room) {
        return buffer_room;
    }
    return record_room;
}

static bool areRecordsWellFormed(const uint8_t *records, uint16_t records_size, uint8_t step_count)
{
    TxnStep step;
    size_t offset = 0;
    size_t size;
    uint8_t i;

    for(i=0; i<step_count; i++) {
        size = TxnStep_Decode(&step, records + offset, records_size - offset);
        if (size == 0) {
            return false;
        }
        offset += size;
    }
    return offset == records_size;
}
