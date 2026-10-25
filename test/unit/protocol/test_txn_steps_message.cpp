#include <cest>
#include <cstring>

extern "C" {
#include "protocol/message_header.h"
#include "protocol/txn_steps_message.h"
}

#define TEST_BUFFER_SIZE 512
#define STEPS_STEP_COUNT_OFFSET 1
#define STEPS_RESERVED_OFFSET 8
#define STEP_TEMPLATE_ID_OFFSET 0
#define FIRST_RECORD_OFFSET (MESSAGE_HEADER_SIZE + TXN_STEPS_FIXED_FIELDS_SIZE)
#define CLASSIC_STEP_RECORD_SIZE 16
#define STEPS_TEMPLATE_INVALID 16

static TxnStep classicTxStep(uint32_t can_id, uint8_t payload_length);
static TxnStep expectOnlyStep(void);
static TxnStep messageRecord(uint8_t payload_length, uint8_t flags);
static size_t encodeSteps(const TxnStep *steps, uint8_t step_count, uint8_t *buffer, size_t buffer_size);
static uint8_t decodeRecords(const TxnStepsMessage *message, TxnStep *steps);
static uint8_t appendUntilRefused(TxnStepsWriter *writer, const TxnStep *step);
static bool areBytesZero(const uint8_t *bytes, size_t count);

static const TxnStepsMessage fixed_fields = { 3, 0, 7, 140000, 0, NULL, 0 };

describe("txn_step", []() {
    it("takes 16 bytes for a classic 8-byte TX step", []() {
        TxnStep step;
        size_t record_size;

        step = classicTxStep(0x7E0, FRAME_PAYLOAD_MAX_CLASSIC);

        record_size = TxnStep_RecordSize(&step);

        expect(record_size).toBe((size_t)CLASSIC_STEP_RECORD_SIZE);
    });

    it("takes 8 bytes for an expect-only step without an expected value", []() {
        TxnStep step;
        size_t record_size;

        step = expectOnlyStep();

        record_size = TxnStep_RecordSize(&step);

        expect(record_size).toBe((size_t)TXN_STEP_RECORD_HEADER_SIZE);
    });

    it("takes 80 bytes for a 64-byte FD step with an expected value", []() {
        TxnStep step;
        size_t record_size;

        step = classicTxStep(0x7E0, FRAME_PAYLOAD_MAX_FD);
        step.frame_flags = FRAME_FLAG_FD;
        step.flags = TXN_STEP_FLAG_EXPECT_VALUE;

        record_size = TxnStep_RecordSize(&step);

        expect(record_size).toBe((size_t)(TXN_STEP_RECORD_HEADER_SIZE + FRAME_PAYLOAD_MAX_FD + TXN_STEP_EXPECT_VALUE_SIZE));
    });

    it("places the expected value after the payload padded to 8 bytes", []() {
        TxnStep step;
        TxnStep decoded;
        uint8_t record[TEST_BUFFER_SIZE];
        size_t encoded_size;
        size_t decoded_size;
        bool padding_zeroed;

        step = classicTxStep(0x7E0, 5);
        step.flags = TXN_STEP_FLAG_EXPECT_VALUE;
        step.expect_value = 0x0123456789ABCDEFULL;
        memset(record, 0xAA, sizeof(record));

        encoded_size = TxnStep_Encode(&step, record, sizeof(record));
        decoded_size = TxnStep_Decode(&decoded, record, encoded_size);
        padding_zeroed = areBytesZero(record + TXN_STEP_RECORD_HEADER_SIZE + 5, TXN_STEP_RECORD_ALIGNMENT - 5);

        expect(encoded_size).toBe((size_t)(TXN_STEP_RECORD_HEADER_SIZE + TXN_STEP_RECORD_ALIGNMENT + TXN_STEP_EXPECT_VALUE_SIZE));
        expect(decoded_size).toBe(encoded_size);
        expect(padding_zeroed).toBe(true);
        expect(decoded.payload_length).toBe(5);
        expect(decoded.payload).toEqual(step.payload);
        expect(decoded.expect_value).toBe(0x0123456789ABCDEFULL);
    });

    it("refuses a classic step longer than 8 bytes", []() {
        TxnStep step;
        uint8_t record[TEST_BUFFER_SIZE];
        size_t encoded_size;

        step = classicTxStep(0x7E0, FRAME_PAYLOAD_MAX_CLASSIC + 1);

        encoded_size = TxnStep_Encode(&step, record, sizeof(record));

        expect(encoded_size).toBe((size_t)0);
    });

    it("refuses an expect-only step that carries a payload", []() {
        TxnStep step;
        uint8_t record[TEST_BUFFER_SIZE];
        size_t encoded_size;

        step = expectOnlyStep();
        step.payload_length = 1;

        encoded_size = TxnStep_Encode(&step, record, sizeof(record));

        expect(encoded_size).toBe((size_t)0);
    });

    it("carries up to 255 bytes of an ISO-TP message in one record", []() {
        TxnStep step;
        TxnStep decoded;
        uint8_t record[TEST_BUFFER_SIZE];
        size_t encoded_size;
        size_t decoded_size;

        step = messageRecord(TXN_STEP_PAYLOAD_SIZE_MAX, TXN_STEP_FLAG_MESSAGE);

        encoded_size = TxnStep_Encode(&step, record, sizeof(record));
        decoded_size = TxnStep_Decode(&decoded, record, encoded_size);

        expect(encoded_size).toBe((size_t)(TXN_STEP_RECORD_HEADER_SIZE + TXN_STEP_PADDED_PAYLOAD_SIZE_MAX));
        expect(decoded_size).toBe(encoded_size);
        expect(decoded.payload_length).toBe(TXN_STEP_PAYLOAD_SIZE_MAX);
        expect(decoded.payload[TXN_STEP_PAYLOAD_SIZE_MAX - 1]).toBe(step.payload[TXN_STEP_PAYLOAD_SIZE_MAX - 1]);
    });

    it("chains a message across records, the expected value on the last one", []() {
        TxnStep steps[2];
        TxnStep decoded[2];
        TxnStepsMessage message;
        uint8_t buffer[TEST_BUFFER_SIZE];
        size_t encoded_size;
        uint8_t decoded_count;

        steps[0] = messageRecord(248, TXN_STEP_FLAG_MESSAGE | TXN_STEP_FLAG_CONTINUED);
        steps[1] = messageRecord(100, TXN_STEP_FLAG_MESSAGE | TXN_STEP_FLAG_EXPECT_VALUE);
        steps[1].expect_value = 0x0176;

        encoded_size = encodeSteps(steps, 2, buffer, sizeof(buffer));
        TxnStepsMessage_Decode(&message, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);
        decoded_count = decodeRecords(&message, decoded);

        expect(decoded_count).toBe(2);
        expect(decoded[0].flags).toBe((uint8_t)(TXN_STEP_FLAG_MESSAGE | TXN_STEP_FLAG_CONTINUED));
        expect(decoded[0].payload_length).toBe(248);
        expect(decoded[1].payload_length).toBe(100);
        expect(decoded[1].expect_value).toBe((uint64_t)0x0176);
    });

    it("refuses a continued record that carries the expected value", []() {
        TxnStep step;
        uint8_t record[TEST_BUFFER_SIZE];
        size_t encoded_size;

        step = messageRecord(8, TXN_STEP_FLAG_MESSAGE | TXN_STEP_FLAG_CONTINUED | TXN_STEP_FLAG_EXPECT_VALUE);

        encoded_size = TxnStep_Encode(&step, record, sizeof(record));

        expect(encoded_size).toBe((size_t)0);
    });

    it("refuses a continued record that is not part of a message", []() {
        TxnStep step;
        uint8_t record[TEST_BUFFER_SIZE];
        size_t encoded_size;

        step = classicTxStep(0x7E0, FRAME_PAYLOAD_MAX_CLASSIC);
        step.flags = TXN_STEP_FLAG_CONTINUED;

        encoded_size = TxnStep_Encode(&step, record, sizeof(record));

        expect(encoded_size).toBe((size_t)0);
    });

    it("refuses a message record with frame flags or no bytes", []() {
        TxnStep with_frame_flags;
        TxnStep empty;
        uint8_t record[TEST_BUFFER_SIZE];
        size_t with_frame_flags_size;
        size_t empty_size;

        with_frame_flags = messageRecord(8, TXN_STEP_FLAG_MESSAGE);
        with_frame_flags.frame_flags = FRAME_FLAG_FD;
        empty = messageRecord(0, TXN_STEP_FLAG_MESSAGE);

        with_frame_flags_size = TxnStep_Encode(&with_frame_flags, record, sizeof(record));
        empty_size = TxnStep_Encode(&empty, record, sizeof(record));

        expect(with_frame_flags_size).toBe((size_t)0);
        expect(empty_size).toBe((size_t)0);
    });

    it("rejects a record naming a template beyond the table", []() {
        TxnStep step;
        TxnStep decoded;
        uint8_t record[TEST_BUFFER_SIZE];
        size_t encoded_size;
        size_t decoded_size;

        step = classicTxStep(0x7E0, FRAME_PAYLOAD_MAX_CLASSIC);
        encoded_size = TxnStep_Encode(&step, record, sizeof(record));
        record[STEP_TEMPLATE_ID_OFFSET] = STEPS_TEMPLATE_INVALID;

        decoded_size = TxnStep_Decode(&decoded, record, encoded_size);

        expect(decoded_size).toBe((size_t)0);
    });
});

describe("txn_steps_message", []() {
    it("round-trips a mixed batch of TX, expect-only and FD steps", []() {
        TxnStep steps[3];
        TxnStep decoded_steps[3];
        TxnStepsMessage decoded;
        uint8_t buffer[TEST_BUFFER_SIZE];
        size_t encoded_size;
        bool decoded_ok;
        uint8_t decoded_count;

        steps[0] = classicTxStep(0x7E0, FRAME_PAYLOAD_MAX_CLASSIC);
        steps[1] = expectOnlyStep();
        steps[1].flags |= TXN_STEP_FLAG_EXPECT_VALUE;
        steps[1].expect_value = 0xC0FFEE;
        steps[2] = classicTxStep(0x18DA10F1 | FRAME_CAN_ID_FLAG_EFF, FRAME_PAYLOAD_MAX_FD);
        steps[2].frame_flags = FRAME_FLAG_FD | FRAME_FLAG_BRS;

        encoded_size = encodeSteps(steps, 3, buffer, sizeof(buffer));
        decoded_ok = TxnStepsMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);
        decoded_count = decodeRecords(&decoded, decoded_steps);

        expect(buffer[0]).toBe((uint8_t)kMESSAGE_TYPE_TXN_STEPS);
        expect(decoded_ok).toBe(true);
        expect(decoded.channel).toBe(3);
        expect(decoded.txn_id).toBe((uint16_t)7);
        expect(decoded.first_seq).toBe((uint32_t)140000);
        expect(decoded.step_count).toBe(3);
        expect(decoded.records_size).toBe((uint16_t)(16 + 16 + 72));
        expect(decoded_count).toBe(3);
        expect(decoded_steps[0].can_id).toBe((uint32_t)0x7E0);
        expect(decoded_steps[0].payload).toEqual(steps[0].payload);
        expect(decoded_steps[1].flags).toBe((uint8_t)(TXN_STEP_FLAG_EXPECT_ONLY | TXN_STEP_FLAG_EXPECT_VALUE));
        expect(decoded_steps[1].expect_value).toBe((uint64_t)0xC0FFEE);
        expect(decoded_steps[2].can_id).toBe((uint32_t)(0x18DA10F1 | FRAME_CAN_ID_FLAG_EFF));
        expect(decoded_steps[2].frame_flags).toBe((uint8_t)(FRAME_FLAG_FD | FRAME_FLAG_BRS));
        expect(decoded_steps[2].flags).toBe((uint8_t)0);
        expect(decoded_steps[2].payload).toEqual(steps[2].payload);
    });

    it("carries the flag that ends the transaction", []() {
        TxnStepsWriter writer;
        TxnStepsMessage decoded;
        uint8_t buffer[TEST_BUFFER_SIZE];
        size_t encoded_size;

        TxnStepsWriter_Init(&writer, &fixed_fields, buffer, sizeof(buffer));
        TxnStepsWriter_MarkEnd(&writer);
        encoded_size = TxnStepsWriter_Finish(&writer);
        TxnStepsMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(buffer[MESSAGE_HEADER_SIZE + 8]).toBe((uint8_t)TXN_STEPS_FLAG_END);
        expect(decoded.flags).toBe((uint8_t)TXN_STEPS_FLAG_END);
        expect(decoded.step_count).toBe(0);
    });

    it("accepts a batch without steps", []() {
        TxnStepsMessage decoded;
        uint8_t buffer[TEST_BUFFER_SIZE];
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = encodeSteps(NULL, 0, buffer, sizeof(buffer));
        decoded_ok = TxnStepsMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(encoded_size).toBe((size_t)FIRST_RECORD_OFFSET);
        expect(decoded_ok).toBe(true);
        expect(decoded.step_count).toBe(0);
        expect(decoded.records_size).toBe((uint16_t)0);
    });

    it("zeroes the reserved bytes of the fixed fields", []() {
        TxnStep step;
        uint8_t buffer[TEST_BUFFER_SIZE];
        bool reserved_zeroed;

        step = classicTxStep(0x7E0, FRAME_PAYLOAD_MAX_CLASSIC);
        memset(buffer, 0xAA, sizeof(buffer));

        encodeSteps(&step, 1, buffer, sizeof(buffer));
        reserved_zeroed = areBytesZero(buffer + MESSAGE_HEADER_SIZE + STEPS_RESERVED_OFFSET, TXN_STEPS_FIXED_FIELDS_SIZE - STEPS_RESERVED_OFFSET);

        expect(reserved_zeroed).toBe(true);
    });

    it("stops appending at the record byte cap", []() {
        TxnStepsWriter writer;
        TxnStep step;
        uint8_t buffer[TEST_BUFFER_SIZE];
        uint8_t appended;
        size_t encoded_size;

        step = classicTxStep(0x7E0, FRAME_PAYLOAD_MAX_CLASSIC);
        TxnStepsWriter_Init(&writer, &fixed_fields, buffer, sizeof(buffer));

        appended = appendUntilRefused(&writer, &step);
        encoded_size = TxnStepsWriter_Finish(&writer);

        expect(appended).toBe(TXN_STEPS_RECORD_BYTES_MAX / CLASSIC_STEP_RECORD_SIZE);
        expect(encoded_size).toBe((size_t)(FIRST_RECORD_OFFSET + TXN_STEPS_RECORD_BYTES_MAX));
    });

    it("stops appending when the caller buffer is full", []() {
        TxnStepsWriter writer;
        TxnStep step;
        uint8_t buffer[FIRST_RECORD_OFFSET + CLASSIC_STEP_RECORD_SIZE + 1];
        uint8_t appended;

        step = classicTxStep(0x7E0, FRAME_PAYLOAD_MAX_CLASSIC);
        TxnStepsWriter_Init(&writer, &fixed_fields, buffer, sizeof(buffer));

        appended = appendUntilRefused(&writer, &step);

        expect(appended).toBe(1);
    });

    it("rejects a step whose payload overruns the message", []() {
        TxnStep step;
        TxnStepsMessage decoded;
        uint8_t buffer[TEST_BUFFER_SIZE];
        size_t encoded_size;
        bool decoded_ok;

        step = classicTxStep(0x7E0, FRAME_PAYLOAD_MAX_CLASSIC);
        encoded_size = encodeSteps(&step, 1, buffer, sizeof(buffer));

        decoded_ok = TxnStepsMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE - 1);

        expect(decoded_ok).toBe(false);
    });

    it("rejects record bytes beyond the announced step count", []() {
        TxnStep steps[2];
        TxnStepsMessage decoded;
        uint8_t buffer[TEST_BUFFER_SIZE];
        size_t encoded_size;
        bool decoded_ok;

        steps[0] = classicTxStep(0x7E0, FRAME_PAYLOAD_MAX_CLASSIC);
        steps[1] = classicTxStep(0x7E0, FRAME_PAYLOAD_MAX_CLASSIC);
        encoded_size = encodeSteps(steps, 2, buffer, sizeof(buffer));
        buffer[MESSAGE_HEADER_SIZE + STEPS_STEP_COUNT_OFFSET] = 1;

        decoded_ok = TxnStepsMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, encoded_size - MESSAGE_HEADER_SIZE);

        expect(decoded_ok).toBe(false);
    });

    it("rejects record bytes above the cap", []() {
        TxnStepsMessage decoded;
        uint8_t payload[TXN_STEPS_BODY_SIZE_MAX + TXN_STEP_RECORD_HEADER_SIZE] = { 0 };
        bool decoded_ok;

        payload[STEPS_STEP_COUNT_OFFSET] = (TXN_STEPS_RECORD_BYTES_MAX / TXN_STEP_RECORD_HEADER_SIZE) + 1;

        decoded_ok = TxnStepsMessage_Decode(&decoded, payload, sizeof(payload));

        expect(decoded_ok).toBe(false);
    });

    it("rejects a payload shorter than the fixed fields", []() {
        TxnStepsMessage decoded;
        uint8_t payload[TXN_STEPS_FIXED_FIELDS_SIZE] = { 0 };
        bool decoded_ok;

        decoded_ok = TxnStepsMessage_Decode(&decoded, payload, TXN_STEPS_FIXED_FIELDS_SIZE - 1);

        expect(decoded_ok).toBe(false);
    });
});

static TxnStep classicTxStep(uint32_t can_id, uint8_t payload_length)
{
    TxnStep step = { 0 };
    uint8_t i;

    step.template_id = 1;
    step.can_id = can_id;
    step.payload_length = payload_length;
    for(i=0; i<payload_length && i<FRAME_PAYLOAD_MAX_FD; i++) {
        step.payload[i] = (uint8_t)(0x10 + i);
    }
    return step;
}

static TxnStep messageRecord(uint8_t payload_length, uint8_t flags)
{
    TxnStep step = { 0 };
    uint16_t i;

    step.template_id = 2;
    step.flags = flags;
    step.can_id = 0x7E0;
    step.payload_length = payload_length;
    for(i=0; i<payload_length; i++) {
        step.payload[i] = (uint8_t)(0x30 + i);
    }
    return step;
}

static TxnStep expectOnlyStep(void)
{
    TxnStep step = { 0 };

    step.template_id = 2;
    step.flags = TXN_STEP_FLAG_EXPECT_ONLY;
    return step;
}

static size_t encodeSteps(const TxnStep *steps, uint8_t step_count, uint8_t *buffer, size_t buffer_size)
{
    TxnStepsWriter writer;
    uint8_t i;

    TxnStepsWriter_Init(&writer, &fixed_fields, buffer, buffer_size);
    for(i=0; i<step_count; i++) {
        TxnStepsWriter_Append(&writer, &steps[i]);
    }
    return TxnStepsWriter_Finish(&writer);
}

static uint8_t decodeRecords(const TxnStepsMessage *message, TxnStep *steps)
{
    size_t offset = 0;
    size_t size;
    uint8_t count = 0;

    while (count < message->step_count) {
        size = TxnStep_Decode(&steps[count], message->records + offset, message->records_size - offset);
        if (size == 0) {
            return count;
        }
        offset += size;
        count++;
    }
    return count;
}

static uint8_t appendUntilRefused(TxnStepsWriter *writer, const TxnStep *step)
{
    uint8_t appended = 0;

    while (TxnStepsWriter_Append(writer, step)) {
        appended++;
    }
    return appended;
}

static bool areBytesZero(const uint8_t *bytes, size_t count)
{
    size_t i;

    for(i=0; i<count; i++) {
        if (bytes[i] != 0) {
            return false;
        }
    }
    return true;
}
