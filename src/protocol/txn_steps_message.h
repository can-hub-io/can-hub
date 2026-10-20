#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol/frame_message.h"

#define TXN_STEPS_FIXED_FIELDS_SIZE 12
#define TXN_STEPS_RECORD_BYTES_MAX 400
#define TXN_STEPS_BODY_SIZE_MAX (TXN_STEPS_FIXED_FIELDS_SIZE + TXN_STEPS_RECORD_BYTES_MAX)
#define TXN_STEP_RECORD_HEADER_SIZE 8
#define TXN_STEP_RECORD_ALIGNMENT 8
#define TXN_STEP_EXPECT_VALUE_SIZE 8
#define TXN_STEP_PAYLOAD_SIZE_MAX 255
#define TXN_STEP_PADDED_PAYLOAD_SIZE_MAX 256
#define TXN_STEP_RECORD_SIZE_MAX (TXN_STEP_RECORD_HEADER_SIZE + TXN_STEP_PADDED_PAYLOAD_SIZE_MAX + TXN_STEP_EXPECT_VALUE_SIZE)

#define TXN_STEP_FLAG_EXPECT_VALUE (1u << 0)
#define TXN_STEP_FLAG_EXPECT_ONLY (1u << 1)
#define TXN_STEP_FLAG_LAST (1u << 2)
#define TXN_STEP_FLAG_MESSAGE (1u << 3)
#define TXN_STEP_FLAG_CONTINUED (1u << 4)

typedef struct {
    uint8_t template_id;
    uint8_t flags;
    uint8_t payload_length;
    uint8_t frame_flags;
    uint32_t can_id;
    uint64_t expect_value;
    uint8_t payload[TXN_STEP_PAYLOAD_SIZE_MAX];
} TxnStep;

typedef struct {
    uint8_t channel;
    uint8_t step_count;
    uint16_t txn_id;
    uint32_t first_seq;
    const uint8_t *records;
    uint16_t records_size;
} TxnStepsMessage;

typedef struct {
    uint8_t *buffer;
    size_t buffer_size;
    uint16_t records_size;
    uint8_t step_count;
} TxnStepsWriter;

size_t TxnStep_RecordSize(const TxnStep *self);
size_t TxnStep_Encode(const TxnStep *self, uint8_t *record, size_t available);
size_t TxnStep_Decode(TxnStep *self, const uint8_t *record, size_t available);

bool TxnStepsWriter_Init(TxnStepsWriter *self, const TxnStepsMessage *message, uint8_t *buffer, size_t buffer_size);
bool TxnStepsWriter_Append(TxnStepsWriter *self, const TxnStep *step);
size_t TxnStepsWriter_Finish(TxnStepsWriter *self);

bool TxnStepsMessage_Decode(TxnStepsMessage *self, const uint8_t *payload, size_t payload_length);
