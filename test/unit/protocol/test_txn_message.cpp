#include <cest>
#include <cstring>

extern "C" {
#include "protocol/message_header.h"
#include "protocol/txn_message.h"
}

#define TEST_BUFFER_SIZE 512
#define BEGIN_ROSTER_COUNT_OFFSET 9
#define BEGIN_RESERVED_OFFSET 10
#define BEGIN_ROSTER_OFFSET 12
#define ROSTER_ENTRY_SIZE 4
#define TEMPLATE_TEMPLATE_ID_OFFSET 1
#define TEMPLATE_RESERVED_OFFSET 17
#define TEMPLATE_RESERVED_SIZE 3
#define STATUS_RX_FRAME_FLAGS_OFFSET 46

static TxnBeginMessage fullRosterBegin(void);
static TxnTemplateMessage udsTemplate(void);
static bool areBytesZero(const uint8_t *bytes, size_t count);

describe("txn_message", []() {
    it("rewrites the channel every transaction message carries first", []() {
        TxnAbortMessage abort_message = { 3, 7 };
        TxnAbortMessage decoded;
        uint8_t buffer[TEST_BUFFER_SIZE];
        uint8_t channel;

        TxnAbortMessage_Encode(&abort_message, buffer, sizeof(buffer));

        TxnMessage_SetChannel(buffer, 9);
        channel = TxnMessage_Channel(buffer + MESSAGE_HEADER_SIZE);
        TxnAbortMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, TXN_ABORT_BODY_SIZE);

        expect(channel).toBe(9);
        expect(decoded.channel).toBe(9);
        expect(decoded.txn_id).toBe((uint16_t)7);
    });
});

describe("txn_begin_message", []() {
    it("round-trips the channel, the flags, the origin extractor and a full roster", []() {
        TxnBeginMessage begin;
        TxnBeginMessage decoded;
        uint8_t buffer[TEST_BUFFER_SIZE];
        size_t encoded_size;
        bool decoded_ok;

        begin = fullRosterBegin();

        encoded_size = TxnBeginMessage_Encode(&begin, buffer, sizeof(buffer));
        decoded_ok = TxnBeginMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, TXN_BEGIN_BODY_SIZE);

        expect(encoded_size).toBe((size_t)(MESSAGE_HEADER_SIZE + TXN_BEGIN_BODY_SIZE));
        expect(buffer[0]).toBe((uint8_t)kMESSAGE_TYPE_TXN_BEGIN);
        expect(decoded_ok).toBe(true);
        expect(decoded.channel).toBe(3);
        expect(decoded.flags).toBe((uint8_t)TXN_BEGIN_FLAG_EXCLUSIVE);
        expect(decoded.origin.source).toBe((uint8_t)kTXN_ORIGIN_SOURCE_PAYLOAD);
        expect(decoded.origin.shift).toBe(4);
        expect(decoded.origin.payload_offset).toBe(2);
        expect(decoded.origin.mask).toBe((uint32_t)0x0000FFF0);
        expect(decoded.roster_count).toBe(TXN_ROSTER_MAX);
        expect(decoded.roster[0]).toBe((uint32_t)0x100);
        expect(decoded.roster[TXN_ROSTER_MAX - 1]).toBe((uint32_t)(0x100 + TXN_ROSTER_MAX - 1));
    });

    it("round-trips a transaction without roster and zeroes the unused roster block", []() {
        TxnBeginMessage begin = { 3, 0, { kTXN_ORIGIN_SOURCE_NONE, 0, 0, 0 }, 0, { 0 } };
        TxnBeginMessage decoded;
        uint8_t buffer[TEST_BUFFER_SIZE];
        size_t encoded_size;
        bool decoded_ok;
        bool roster_zeroed;
        bool reserved_zeroed;

        memset(buffer, 0xAA, sizeof(buffer));

        encoded_size = TxnBeginMessage_Encode(&begin, buffer, sizeof(buffer));
        decoded_ok = TxnBeginMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, TXN_BEGIN_BODY_SIZE);
        roster_zeroed = areBytesZero(buffer + MESSAGE_HEADER_SIZE + BEGIN_ROSTER_OFFSET, TXN_ROSTER_MAX * ROSTER_ENTRY_SIZE);
        reserved_zeroed = areBytesZero(buffer + MESSAGE_HEADER_SIZE + BEGIN_RESERVED_OFFSET, BEGIN_ROSTER_OFFSET - BEGIN_RESERVED_OFFSET);

        expect(encoded_size).toBe((size_t)(MESSAGE_HEADER_SIZE + TXN_BEGIN_BODY_SIZE));
        expect(decoded_ok).toBe(true);
        expect(decoded.roster_count).toBe(0);
        expect(roster_zeroed).toBe(true);
        expect(reserved_zeroed).toBe(true);
    });

    it("refuses to encode a roster above the maximum", []() {
        TxnBeginMessage begin;
        uint8_t buffer[TEST_BUFFER_SIZE];
        size_t encoded_size;

        begin = fullRosterBegin();
        begin.roster_count = TXN_ROSTER_MAX + 1;

        encoded_size = TxnBeginMessage_Encode(&begin, buffer, sizeof(buffer));

        expect(encoded_size).toBe((size_t)0);
    });

    it("rejects a roster count above the maximum", []() {
        TxnBeginMessage begin;
        TxnBeginMessage decoded;
        uint8_t buffer[TEST_BUFFER_SIZE];
        bool decoded_ok;

        begin = fullRosterBegin();
        TxnBeginMessage_Encode(&begin, buffer, sizeof(buffer));
        buffer[MESSAGE_HEADER_SIZE + BEGIN_ROSTER_COUNT_OFFSET] = TXN_ROSTER_MAX + 1;

        decoded_ok = TxnBeginMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, TXN_BEGIN_BODY_SIZE);

        expect(decoded_ok).toBe(false);
    });

    it("rejects a truncated begin payload", []() {
        TxnBeginMessage decoded;
        uint8_t payload[TXN_BEGIN_BODY_SIZE] = { 0 };
        bool decoded_ok;

        decoded_ok = TxnBeginMessage_Decode(&decoded, payload, TXN_BEGIN_BODY_SIZE - 1);

        expect(decoded_ok).toBe(false);
    });
});

describe("txn_begin_ack_message", []() {
    it("round-trips the channel, the status, the transaction id and the window in bytes", []() {
        TxnBeginAckMessage ack = { 3, kTXN_BEGIN_STATUS_OK, 0xBEEF, 32768 };
        TxnBeginAckMessage decoded;
        uint8_t buffer[TEST_BUFFER_SIZE];
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = TxnBeginAckMessage_Encode(&ack, buffer, sizeof(buffer));
        decoded_ok = TxnBeginAckMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, TXN_BEGIN_ACK_BODY_SIZE);

        expect(encoded_size).toBe((size_t)(MESSAGE_HEADER_SIZE + TXN_BEGIN_ACK_BODY_SIZE));
        expect(buffer[0]).toBe((uint8_t)kMESSAGE_TYPE_TXN_BEGIN_ACK);
        expect(decoded_ok).toBe(true);
        expect(decoded.channel).toBe(3);
        expect(decoded.status).toBe((uint8_t)kTXN_BEGIN_STATUS_OK);
        expect(decoded.txn_id).toBe((uint16_t)0xBEEF);
        expect(decoded.window_bytes).toBe((uint32_t)32768);
    });

    it("round-trips the reliable-channel-required rejection", []() {
        TxnBeginAckMessage ack = { 3, kTXN_BEGIN_STATUS_RELIABLE_CHANNEL_REQUIRED, 0, 0 };
        TxnBeginAckMessage decoded;
        uint8_t buffer[TEST_BUFFER_SIZE];
        bool decoded_ok;

        TxnBeginAckMessage_Encode(&ack, buffer, sizeof(buffer));
        decoded_ok = TxnBeginAckMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, TXN_BEGIN_ACK_BODY_SIZE);

        expect(decoded_ok).toBe(true);
        expect(decoded.status).toBe((uint8_t)kTXN_BEGIN_STATUS_RELIABLE_CHANNEL_REQUIRED);
    });

    it("rejects a truncated begin ack payload", []() {
        TxnBeginAckMessage decoded;
        uint8_t payload[TXN_BEGIN_ACK_BODY_SIZE] = { 0 };
        bool decoded_ok;

        decoded_ok = TxnBeginAckMessage_Decode(&decoded, payload, TXN_BEGIN_ACK_BODY_SIZE - 1);

        expect(decoded_ok).toBe(false);
    });
});

describe("txn_template_message", []() {
    it("round-trips the advance settings, the timing and the three patterns", []() {
        TxnTemplateMessage template_message;
        TxnTemplateMessage decoded;
        uint8_t buffer[TEST_BUFFER_SIZE];
        size_t encoded_size;
        bool decoded_ok;

        template_message = udsTemplate();

        encoded_size = TxnTemplateMessage_Encode(&template_message, buffer, sizeof(buffer));
        decoded_ok = TxnTemplateMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, TXN_TEMPLATE_BODY_SIZE);

        expect(encoded_size).toBe((size_t)(MESSAGE_HEADER_SIZE + TXN_TEMPLATE_BODY_SIZE));
        expect(buffer[0]).toBe((uint8_t)kMESSAGE_TYPE_TXN_TEMPLATE);
        expect(decoded_ok).toBe(true);
        expect(decoded.channel).toBe(3);
        expect(decoded.template_id).toBe(TXN_TEMPLATES_MAX - 1);
        expect(decoded.txn_id).toBe((uint16_t)7);
        expect(decoded.advance_mode).toBe((uint8_t)kTXN_ADVANCE_MODE_COUNT);
        expect(decoded.flags).toBe((uint8_t)(TXN_TEMPLATE_FLAG_SOFT | TXN_TEMPLATE_FLAG_MISMATCH_ABORT));
        expect(decoded.count_k).toBe(1);
        expect(decoded.patterns_present).toBe((uint8_t)0x07);
        expect(decoded.timeout_ms).toBe((uint32_t)50);
        expect(decoded.gap_us).toBe((uint32_t)1000);
        expect(decoded.type).toBe(0);
        expect(decoded.patterns[kTXN_PATTERN_ADVANCE].can_id).toBe((uint32_t)0x7E8);
        expect(decoded.patterns[kTXN_PATTERN_ADVANCE].payload_mask).toBe((uint64_t)0xFF00);
        expect(decoded.patterns[kTXN_PATTERN_ABORT].payload_value).toBe((uint64_t)0x7F00);
        expect(decoded.patterns[kTXN_PATTERN_EXTEND].can_mask).toBe((uint32_t)0x7FF);
        expect(decoded.patterns[kTXN_PATTERN_EXTEND].payload_value).toBe((uint64_t)0x78007F00);
    });

    it("zeroes the reserved bytes", []() {
        TxnTemplateMessage template_message;
        uint8_t buffer[TEST_BUFFER_SIZE];
        bool reserved_zeroed;

        template_message = udsTemplate();
        memset(buffer, 0xAA, sizeof(buffer));

        TxnTemplateMessage_Encode(&template_message, buffer, sizeof(buffer));
        reserved_zeroed = areBytesZero(buffer + MESSAGE_HEADER_SIZE + TEMPLATE_RESERVED_OFFSET, TEMPLATE_RESERVED_SIZE);

        expect(reserved_zeroed).toBe(true);
    });

    it("refuses to encode a template id beyond the table", []() {
        TxnTemplateMessage template_message;
        uint8_t buffer[TEST_BUFFER_SIZE];
        size_t encoded_size;

        template_message = udsTemplate();
        template_message.template_id = TXN_TEMPLATES_MAX;

        encoded_size = TxnTemplateMessage_Encode(&template_message, buffer, sizeof(buffer));

        expect(encoded_size).toBe((size_t)0);
    });

    it("rejects a template id beyond the table", []() {
        TxnTemplateMessage template_message;
        TxnTemplateMessage decoded;
        uint8_t buffer[TEST_BUFFER_SIZE];
        bool decoded_ok;

        template_message = udsTemplate();
        TxnTemplateMessage_Encode(&template_message, buffer, sizeof(buffer));
        buffer[MESSAGE_HEADER_SIZE + TEMPLATE_TEMPLATE_ID_OFFSET] = TXN_TEMPLATES_MAX;

        decoded_ok = TxnTemplateMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, TXN_TEMPLATE_BODY_SIZE);

        expect(decoded_ok).toBe(false);
    });

    it("rejects a truncated template payload", []() {
        TxnTemplateMessage decoded;
        uint8_t payload[TXN_TEMPLATE_BODY_SIZE] = { 0 };
        bool decoded_ok;

        decoded_ok = TxnTemplateMessage_Decode(&decoded, payload, TXN_TEMPLATE_BODY_SIZE - 1);

        expect(decoded_ok).toBe(false);
    });
});

describe("txn_progress_message", []() {
    it("round-trips the completed steps and the consumed bytes", []() {
        TxnProgressMessage progress = { 3, 7, 150000, 0xFFFFFFF0 };
        TxnProgressMessage decoded;
        uint8_t buffer[TEST_BUFFER_SIZE];
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = TxnProgressMessage_Encode(&progress, buffer, sizeof(buffer));
        decoded_ok = TxnProgressMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, TXN_PROGRESS_BODY_SIZE);

        expect(encoded_size).toBe((size_t)(MESSAGE_HEADER_SIZE + TXN_PROGRESS_BODY_SIZE));
        expect(buffer[0]).toBe((uint8_t)kMESSAGE_TYPE_TXN_PROGRESS);
        expect(decoded_ok).toBe(true);
        expect(decoded.channel).toBe(3);
        expect(decoded.txn_id).toBe((uint16_t)7);
        expect(decoded.completed_steps).toBe((uint32_t)150000);
        expect(decoded.consumed_bytes).toBe((uint32_t)0xFFFFFFF0);
    });

    it("rejects a truncated progress payload", []() {
        TxnProgressMessage decoded;
        uint8_t payload[TXN_PROGRESS_BODY_SIZE] = { 0 };
        bool decoded_ok;

        decoded_ok = TxnProgressMessage_Decode(&decoded, payload, TXN_PROGRESS_BODY_SIZE - 1);

        expect(decoded_ok).toBe(false);
    });
});

describe("txn_status_message", []() {
    it("round-trips the failure report with a 64-byte FD frame", []() {
        TxnStatusMessage status = { 0 };
        TxnStatusMessage decoded;
        uint8_t buffer[TEST_BUFFER_SIZE];
        size_t encoded_size;
        bool decoded_ok;
        uint8_t i;

        status.channel = 3;
        status.state = kTXN_STATE_ABORTED;
        status.txn_id = 7;
        status.failing_seq = 140001;
        status.offending_origin = 0x21;
        status.seen_bitmap = 0x7FFFFFFFFFFFFFFFULL;
        status.expected = 0xA5A5;
        status.got = 0x5A5A;
        status.completed_steps = 140000;
        status.reason = kTXN_REASON_VALUE_MISMATCH;
        status.flags = TXN_STATUS_FLAG_HAS_RX_FRAME;
        status.rx_frame.can_id = 0x18FF0021 | FRAME_CAN_ID_FLAG_EFF;
        status.rx_frame.payload_length = FRAME_PAYLOAD_MAX_FD;
        status.rx_frame.frame_flags = FRAME_FLAG_FD;
        for(i=0; i<FRAME_PAYLOAD_MAX_FD; i++) {
            status.rx_frame.payload[i] = i;
        }

        encoded_size = TxnStatusMessage_Encode(&status, buffer, sizeof(buffer));
        decoded_ok = TxnStatusMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, TXN_STATUS_BODY_SIZE);

        expect(encoded_size).toBe((size_t)(MESSAGE_HEADER_SIZE + TXN_STATUS_BODY_SIZE));
        expect(buffer[0]).toBe((uint8_t)kMESSAGE_TYPE_TXN_STATUS);
        expect(decoded_ok).toBe(true);
        expect(decoded.channel).toBe(3);
        expect(decoded.state).toBe((uint8_t)kTXN_STATE_ABORTED);
        expect(decoded.txn_id).toBe((uint16_t)7);
        expect(decoded.failing_seq).toBe((uint32_t)140001);
        expect(decoded.offending_origin).toBe((uint32_t)0x21);
        expect(decoded.seen_bitmap).toBe(0x7FFFFFFFFFFFFFFFULL);
        expect(decoded.expected).toBe((uint64_t)0xA5A5);
        expect(decoded.got).toBe((uint64_t)0x5A5A);
        expect(decoded.completed_steps).toBe((uint32_t)140000);
        expect(decoded.reason).toBe((uint8_t)kTXN_REASON_VALUE_MISMATCH);
        expect(decoded.flags).toBe((uint8_t)TXN_STATUS_FLAG_HAS_RX_FRAME);
        expect(decoded.rx_frame.can_id).toBe((uint32_t)(0x18FF0021 | FRAME_CAN_ID_FLAG_EFF));
        expect(decoded.rx_frame.payload_length).toBe(FRAME_PAYLOAD_MAX_FD);
        expect(decoded.rx_frame.frame_flags).toBe((uint8_t)FRAME_FLAG_FD);
        expect(decoded.rx_frame.payload).toEqual(status.rx_frame.payload);
    });

    it("refuses to encode a classic frame longer than 8 bytes", []() {
        TxnStatusMessage status = { 0 };
        uint8_t buffer[TEST_BUFFER_SIZE];
        size_t encoded_size;

        status.rx_frame.payload_length = FRAME_PAYLOAD_MAX_CLASSIC + 1;

        encoded_size = TxnStatusMessage_Encode(&status, buffer, sizeof(buffer));

        expect(encoded_size).toBe((size_t)0);
    });

    it("rejects a classic frame longer than 8 bytes", []() {
        TxnStatusMessage status = { 0 };
        TxnStatusMessage decoded;
        uint8_t buffer[TEST_BUFFER_SIZE];
        bool decoded_ok;

        status.rx_frame.payload_length = FRAME_PAYLOAD_MAX_FD;
        status.rx_frame.frame_flags = FRAME_FLAG_FD;
        TxnStatusMessage_Encode(&status, buffer, sizeof(buffer));
        buffer[MESSAGE_HEADER_SIZE + STATUS_RX_FRAME_FLAGS_OFFSET] = 0;

        decoded_ok = TxnStatusMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, TXN_STATUS_BODY_SIZE);

        expect(decoded_ok).toBe(false);
    });

    it("rejects a truncated status payload", []() {
        TxnStatusMessage decoded;
        uint8_t payload[TXN_STATUS_BODY_SIZE] = { 0 };
        bool decoded_ok;

        decoded_ok = TxnStatusMessage_Decode(&decoded, payload, TXN_STATUS_BODY_SIZE - 1);

        expect(decoded_ok).toBe(false);
    });
});

describe("txn_abort_message", []() {
    it("round-trips the channel and the transaction id", []() {
        TxnAbortMessage abort_message = { 3, 7 };
        TxnAbortMessage decoded;
        uint8_t buffer[TEST_BUFFER_SIZE];
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = TxnAbortMessage_Encode(&abort_message, buffer, sizeof(buffer));
        decoded_ok = TxnAbortMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, TXN_ABORT_BODY_SIZE);

        expect(encoded_size).toBe((size_t)(MESSAGE_HEADER_SIZE + TXN_ABORT_BODY_SIZE));
        expect(buffer[0]).toBe((uint8_t)kMESSAGE_TYPE_TXN_ABORT);
        expect(decoded_ok).toBe(true);
        expect(decoded.channel).toBe(3);
        expect(decoded.txn_id).toBe((uint16_t)7);
    });

    it("rejects a truncated abort payload", []() {
        TxnAbortMessage decoded;
        uint8_t payload[TXN_ABORT_BODY_SIZE] = { 0 };
        bool decoded_ok;

        decoded_ok = TxnAbortMessage_Decode(&decoded, payload, TXN_ABORT_BODY_SIZE - 1);

        expect(decoded_ok).toBe(false);
    });
});

static TxnBeginMessage fullRosterBegin(void)
{
    TxnBeginMessage begin = { 0 };
    uint8_t i;

    begin.channel = 3;
    begin.flags = TXN_BEGIN_FLAG_EXCLUSIVE;
    begin.origin.source = kTXN_ORIGIN_SOURCE_PAYLOAD;
    begin.origin.shift = 4;
    begin.origin.payload_offset = 2;
    begin.origin.mask = 0x0000FFF0;
    begin.roster_count = TXN_ROSTER_MAX;
    for(i=0; i<TXN_ROSTER_MAX; i++) {
        begin.roster[i] = 0x100 + i;
    }
    return begin;
}

static TxnTemplateMessage udsTemplate(void)
{
    TxnTemplateMessage template_message = { 0 };

    template_message.channel = 3;
    template_message.template_id = TXN_TEMPLATES_MAX - 1;
    template_message.txn_id = 7;
    template_message.advance_mode = kTXN_ADVANCE_MODE_COUNT;
    template_message.flags = TXN_TEMPLATE_FLAG_SOFT | TXN_TEMPLATE_FLAG_MISMATCH_ABORT;
    template_message.count_k = 1;
    template_message.patterns_present = 0x07;
    template_message.timeout_ms = 50;
    template_message.gap_us = 1000;
    template_message.patterns[kTXN_PATTERN_ADVANCE] = { 0x7E8, 0x7FF, 0xFF00, 0x7600 };
    template_message.patterns[kTXN_PATTERN_ABORT] = { 0x7E8, 0x7FF, 0xFF00, 0x7F00 };
    template_message.patterns[kTXN_PATTERN_EXTEND] = { 0x7E8, 0x7FF, 0xFF00FF00, 0x78007F00 };
    return template_message;
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
