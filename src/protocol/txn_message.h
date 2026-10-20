#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol/frame_message.h"

#define TXN_BEGIN_BODY_SIZE 268
#define TXN_BEGIN_ACK_BODY_SIZE 8
#define TXN_TEMPLATE_BODY_SIZE 108
#define TXN_PROGRESS_BODY_SIZE 12
#define TXN_STATUS_BODY_SIZE 112
#define TXN_ABORT_BODY_SIZE 4

#define TXN_ROSTER_MAX 64
#define TXN_TEMPLATES_MAX 16
#define TXN_PATTERN_PAYLOAD_BYTES 8
#define TXN_CHANNEL_OFFSET 0

#define TXN_BEGIN_FLAG_EXCLUSIVE (1u << 0)
#define TXN_BEGIN_FLAG_ISOTP (1u << 1)
#define TXN_TEMPLATE_FLAG_SOFT (1u << 0)
#define TXN_TEMPLATE_FLAG_MISMATCH_ABORT (1u << 1)
#define TXN_STATUS_FLAG_HAS_RX_FRAME (1u << 0)
#define TXN_ISOTP_FLAG_PADDING (1u << 0)
#define TXN_ISOTP_FLAG_ADDRESS_BYTE (1u << 1)
#define TXN_ISOTP_FLAG_FD (1u << 2)
#define TXN_ISOTP_FLAG_BIT_RATE_SWITCH (1u << 3)
#define TXN_ISOTP_MESSAGE_SIZE_MAX 4095

typedef enum ttxn_origin_source_e {
    kTXN_ORIGIN_SOURCE_NONE = 0,
    kTXN_ORIGIN_SOURCE_CAN_ID = 1,
    kTXN_ORIGIN_SOURCE_PAYLOAD = 2,
    kTXN_ORIGIN_SOURCE_MAX,
} TTXN_ORIGIN_SOURCE;

typedef enum ttxn_begin_status_e {
    kTXN_BEGIN_STATUS_OK = 0,
    kTXN_BEGIN_STATUS_BUSY = 1,
    kTXN_BEGIN_STATUS_WRITE_DENIED = 2,
    kTXN_BEGIN_STATUS_UNSUPPORTED = 3,
    kTXN_BEGIN_STATUS_RELIABLE_CHANNEL_REQUIRED = 4,
    kTXN_BEGIN_STATUS_INVALID = 5,
    kTXN_BEGIN_STATUS_MAX,
} TTXN_BEGIN_STATUS;

typedef enum ttxn_template_type_e {
    kTXN_TEMPLATE_TYPE_GENERIC = 0,
    kTXN_TEMPLATE_TYPE_ISOTP = 1,
    kTXN_TEMPLATE_TYPE_MAX,
} TTXN_TEMPLATE_TYPE;

typedef enum ttxn_advance_mode_e {
    kTXN_ADVANCE_MODE_NONE = 0,
    kTXN_ADVANCE_MODE_COUNT = 1,
    kTXN_ADVANCE_MODE_ROSTER_ALL = 2,
    kTXN_ADVANCE_MODE_ROSTER_K = 3,
    kTXN_ADVANCE_MODE_MAX,
} TTXN_ADVANCE_MODE;

typedef enum ttxn_pattern_e {
    kTXN_PATTERN_ADVANCE = 0,
    kTXN_PATTERN_ABORT = 1,
    kTXN_PATTERN_EXTEND = 2,
    kTXN_PATTERN_MAX,
} TTXN_PATTERN;

typedef enum ttxn_state_e {
    kTXN_STATE_DONE = 0,
    kTXN_STATE_ABORTED = 1,
    kTXN_STATE_MAX,
} TTXN_STATE;

typedef enum ttxn_reason_e {
    kTXN_REASON_NONE = 0,
    kTXN_REASON_TIMEOUT = 1,
    kTXN_REASON_ABORT_MATCH = 2,
    kTXN_REASON_VALUE_MISMATCH = 3,
    kTXN_REASON_TX_FAILED = 4,
    kTXN_REASON_CANCELLED = 5,
    kTXN_REASON_STARVED = 6,
    kTXN_REASON_OVERRUN = 7,
    kTXN_REASON_AGENT_DISCONNECTED = 8,
    kTXN_REASON_INVALID_STEP = 9,
    kTXN_REASON_FLOW_CONTROL_TIMEOUT = 10,
    kTXN_REASON_CONSECUTIVE_FRAME_TIMEOUT = 11,
    kTXN_REASON_OVERFLOW = 12,
    kTXN_REASON_SEQUENCE_ERROR = 13,
    kTXN_REASON_WAIT_LIMIT = 14,
    kTXN_REASON_MAX,
} TTXN_REASON;

typedef struct {
    uint8_t source;
    uint8_t shift;
    uint8_t payload_offset;
    uint32_t mask;
} TxnOriginExtractor;

typedef struct {
    uint8_t channel;
    uint8_t flags;
    TxnOriginExtractor origin;
    uint8_t roster_count;
    uint32_t roster[TXN_ROSTER_MAX];
} TxnBeginMessage;

typedef struct {
    uint8_t channel;
    uint8_t status;
    uint16_t txn_id;
    uint32_t window_bytes;
} TxnBeginAckMessage;

typedef struct {
    uint32_t can_id;
    uint32_t can_mask;
    uint64_t payload_mask;
    uint64_t payload_value;
} TxnPattern;

typedef struct {
    uint32_t response_id;
    uint8_t flags;
    uint8_t tx_address;
    uint8_t rx_address;
    uint8_t padding;
    uint8_t tx_data_length;
    uint8_t block_size;
    uint8_t st_min;
} TxnIsotpParameters;

typedef struct {
    uint8_t channel;
    uint8_t template_id;
    uint16_t txn_id;
    uint8_t advance_mode;
    uint8_t flags;
    uint8_t count_k;
    uint8_t patterns_present;
    uint32_t timeout_ms;
    uint32_t gap_us;
    uint32_t extend_timeout_ms;
    uint8_t type;
    TxnPattern patterns[kTXN_PATTERN_MAX];
    TxnIsotpParameters isotp;
} TxnTemplateMessage;

typedef struct {
    uint8_t channel;
    uint16_t txn_id;
    uint32_t completed_steps;
    uint32_t consumed_bytes;
} TxnProgressMessage;

typedef struct {
    uint32_t can_id;
    uint8_t payload_length;
    uint8_t frame_flags;
    uint8_t payload[FRAME_PAYLOAD_MAX_FD];
} TxnRxFrame;

typedef struct {
    uint8_t channel;
    uint8_t state;
    uint16_t txn_id;
    uint32_t failing_seq;
    uint32_t offending_origin;
    uint64_t seen_bitmap;
    uint64_t expected;
    uint64_t got;
    uint32_t completed_steps;
    uint8_t reason;
    uint8_t flags;
    TxnRxFrame rx_frame;
} TxnStatusMessage;

typedef struct {
    uint8_t channel;
    uint16_t txn_id;
} TxnAbortMessage;

void TxnMessage_SetChannel(uint8_t *encoded, uint8_t channel);
uint8_t TxnMessage_Channel(const uint8_t *payload);

size_t TxnBeginMessage_Encode(const TxnBeginMessage *self, uint8_t *buffer, size_t buffer_size);
bool TxnBeginMessage_Decode(TxnBeginMessage *self, const uint8_t *payload, size_t payload_length);

size_t TxnBeginAckMessage_Encode(const TxnBeginAckMessage *self, uint8_t *buffer, size_t buffer_size);
bool TxnBeginAckMessage_Decode(TxnBeginAckMessage *self, const uint8_t *payload, size_t payload_length);

size_t TxnTemplateMessage_Encode(const TxnTemplateMessage *self, uint8_t *buffer, size_t buffer_size);
bool TxnTemplateMessage_Decode(TxnTemplateMessage *self, const uint8_t *payload, size_t payload_length);

size_t TxnProgressMessage_Encode(const TxnProgressMessage *self, uint8_t *buffer, size_t buffer_size);
bool TxnProgressMessage_Decode(TxnProgressMessage *self, const uint8_t *payload, size_t payload_length);

size_t TxnStatusMessage_Encode(const TxnStatusMessage *self, uint8_t *buffer, size_t buffer_size);
bool TxnStatusMessage_Decode(TxnStatusMessage *self, const uint8_t *payload, size_t payload_length);

size_t TxnAbortMessage_Encode(const TxnAbortMessage *self, uint8_t *buffer, size_t buffer_size);
bool TxnAbortMessage_Decode(TxnAbortMessage *self, const uint8_t *payload, size_t payload_length);
