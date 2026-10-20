#ifndef CANHUB_H
#define CANHUB_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CANHUB_API_VERSION 1

#if defined(_WIN32)
#define CANHUB_API __declspec(dllexport)
#elif defined(__GNUC__)
#define CANHUB_API __attribute__((visibility("default")))
#else
#define CANHUB_API
#endif

#define CANHUB_FRAME_PAYLOAD_MAX 64
#define CANHUB_AGENT_NAME_MAX 128
#define CANHUB_INTERFACE_NAME_MAX 16
#define CANHUB_FILTERS_MAX 16

#define CANHUB_CAN_ID_MASK 0x1FFFFFFFu
#define CANHUB_CAN_ID_FLAG_ERR (1u << 29)
#define CANHUB_CAN_ID_FLAG_RTR (1u << 30)
#define CANHUB_CAN_ID_FLAG_EFF (1u << 31)

#define CANHUB_FRAME_FLAG_FD (1u << 0)
#define CANHUB_FRAME_FLAG_BRS (1u << 1)
/* Received only: the hub injected this frame along a bridge route or group. */
#define CANHUB_FRAME_FLAG_BRIDGED (1u << 7)

#define CANHUB_OPEN_FLAG_NO_ECHO (1u << 0)
#define CANHUB_OPEN_FLAG_WRITE (1u << 1)
#define CANHUB_OPEN_FLAG_RELIABLE (1u << 2)
#define CANHUB_OPEN_FLAG_ERROR_FRAMES (1u << 3)

#define CANHUB_OK 0
#define CANHUB_RECEIVED 1
#define CANHUB_ERR_TIMEOUT (-1)
#define CANHUB_ERR_DISCONNECTED (-2)
#define CANHUB_ERR_NOT_FOUND (-3)
#define CANHUB_ERR_OPEN_REJECTED (-4)
#define CANHUB_ERR_WRITE_DENIED (-5)
#define CANHUB_ERR_READ_DENIED (-6)
#define CANHUB_ERR_ARGUMENT (-7)
#define CANHUB_ERR_STATE (-8)
#define CANHUB_ERR_TRANSPORT (-9)
#define CANHUB_ERR_HUB (-10)
#define CANHUB_ERR_RELIABLE_UNSUPPORTED (-11)
#define CANHUB_ERR_RATE_LIMITED (-12)
#define CANHUB_ERR_CONFIGURE_DENIED (-13)
#define CANHUB_ERR_APPLY_FAILED (-14)
#define CANHUB_ERR_AGENT_UNREACHABLE (-15)
#define CANHUB_ERR_LEASED (-16)
#define CANHUB_ERR_TXN_BUSY (-17)
#define CANHUB_ERR_TXN_UNSUPPORTED (-18)
#define CANHUB_ERR_TXN_ABORTED (-19)

#define CANHUB_TXN_ROSTER_MAX 64
#define CANHUB_TXN_TEMPLATES_MAX 16
#define CANHUB_TXN_FLAG_EXCLUSIVE (1u << 0)
#define CANHUB_TXN_FLAG_ISOTP (1u << 1)
#define CANHUB_TXN_TYPE_GENERIC 0
#define CANHUB_TXN_TYPE_ISOTP 1
#define CANHUB_ISOTP_FLAG_PADDING (1u << 0)
#define CANHUB_ISOTP_FLAG_ADDRESS_BYTE (1u << 1)
#define CANHUB_ISOTP_FLAG_FD (1u << 2)
#define CANHUB_ISOTP_FLAG_BIT_RATE_SWITCH (1u << 3)
#define CANHUB_ISOTP_MESSAGE_MAX 4095
#define CANHUB_UDS_EXPECT_MAX 8
#define CANHUB_UDS_TRANSFER_BLOCK_MAX 4093
#define CANHUB_TXN_ORIGIN_NONE 0
#define CANHUB_TXN_ORIGIN_CAN_ID 1
#define CANHUB_TXN_ORIGIN_PAYLOAD 2
#define CANHUB_TXN_ADVANCE_NONE 0
#define CANHUB_TXN_ADVANCE_COUNT 1
#define CANHUB_TXN_ADVANCE_ROSTER_ALL 2
#define CANHUB_TXN_ADVANCE_ROSTER_K 3
#define CANHUB_TXN_TEMPLATE_SOFT (1u << 0)
#define CANHUB_TXN_TEMPLATE_MISMATCH_ABORT (1u << 1)
#define CANHUB_TXN_PATTERN_ADVANCE 0
#define CANHUB_TXN_PATTERN_ABORT 1
#define CANHUB_TXN_PATTERN_EXTEND 2
#define CANHUB_TXN_PATTERNS 3
#define CANHUB_TXN_STEP_EXPECT_VALUE (1u << 0)
#define CANHUB_TXN_STEP_EXPECT_ONLY (1u << 1)
#define CANHUB_TXN_DONE 0
#define CANHUB_TXN_ABORTED 1
#define CANHUB_TXN_REASON_NONE 0
#define CANHUB_TXN_REASON_TIMEOUT 1
#define CANHUB_TXN_REASON_ABORT_MATCH 2
#define CANHUB_TXN_REASON_VALUE_MISMATCH 3
#define CANHUB_TXN_REASON_TX_FAILED 4
#define CANHUB_TXN_REASON_CANCELLED 5
#define CANHUB_TXN_REASON_STARVED 6
#define CANHUB_TXN_REASON_OVERRUN 7
#define CANHUB_TXN_REASON_AGENT_DISCONNECTED 8
#define CANHUB_TXN_REASON_INVALID_STEP 9
#define CANHUB_TXN_REASON_FLOW_CONTROL_TIMEOUT 10
#define CANHUB_TXN_REASON_CONSECUTIVE_FRAME_TIMEOUT 11
#define CANHUB_TXN_REASON_OVERFLOW 12
#define CANHUB_TXN_REASON_SEQUENCE_ERROR 13
#define CANHUB_TXN_REASON_WAIT_LIMIT 14
#define CANHUB_SCRIPT_NAME_MAX 32

#define CANHUB_SETTING_BITRATE (1u << 0)
#define CANHUB_SETTING_SAMPLE_POINT (1u << 1)
#define CANHUB_SETTING_DATA_BITRATE (1u << 2)
#define CANHUB_SETTING_DATA_SAMPLE_POINT (1u << 3)
#define CANHUB_SETTING_CTRLMODE (1u << 4)
#define CANHUB_SETTING_RESTART_MS (1u << 5)
#define CANHUB_SETTING_TXQUEUELEN (1u << 6)
#define CANHUB_SETTING_LINK (1u << 7)
#define CANHUB_SETTING_STATE (1u << 8)
#define CANHUB_SETTING_BIT_TIMING (1u << 9)
#define CANHUB_SETTING_DATA_BIT_TIMING (1u << 10)
#define CANHUB_SETTING_TERMINATION (1u << 11)

#define CANHUB_CTRLMODE_LOOPBACK (1u << 0)
#define CANHUB_CTRLMODE_LISTEN_ONLY (1u << 1)
#define CANHUB_CTRLMODE_TRIPLE_SAMPLING (1u << 2)
#define CANHUB_CTRLMODE_ONE_SHOT (1u << 3)
#define CANHUB_CTRLMODE_BERR_REPORTING (1u << 4)
#define CANHUB_CTRLMODE_FD (1u << 5)

#define CANHUB_STATE_ERROR_ACTIVE 0
#define CANHUB_STATE_ERROR_WARNING 1
#define CANHUB_STATE_ERROR_PASSIVE 2
#define CANHUB_STATE_BUS_OFF 3
#define CANHUB_STATE_STOPPED 4
#define CANHUB_STATE_SLEEPING 5

typedef struct CanHubSession CanHubSession;

typedef struct {
    uint64_t timestamp_us;
    uint32_t can_id;
    uint8_t flags;
    uint8_t length;
    uint8_t reserved[2];
    uint8_t payload[CANHUB_FRAME_PAYLOAD_MAX];
} CanHubFrame;

typedef struct {
    uint32_t interface_id;
    char agent[CANHUB_AGENT_NAME_MAX];
    char interface_name[CANHUB_INTERFACE_NAME_MAX];
} CanHubInterfaceInfo;

typedef struct {
    uint32_t can_id;
    uint32_t can_mask;
} CanHubFilter;

typedef struct {
    uint32_t struct_size;
    uint64_t frames_rate_limited;
    uint64_t frames_ring_dropped;
} CanHubStats;

typedef struct {
    uint32_t struct_size;
    const char *url;
    const char *state_directory;
    const char *certificate_path;
    const char *key_path;
    const char *hub_fingerprint;
    int32_t connect_timeout_ms;
} CanHubConnectConfig;

/*
 * Raw bit timing, the alternative to a bitrate: time quantum in ns and the
 * segments in quanta. Give either a bitrate (and sample point) or tq and the
 * segments, not both; sjw may go with either. Replies carry the timing the
 * kernel settled on, brp included.
 */
typedef struct {
    uint32_t tq;
    uint32_t prop_seg;
    uint32_t phase_seg1;
    uint32_t phase_seg2;
    uint32_t sjw;
    uint32_t brp;
} CanHubBitTiming;

/*
 * The settings of one CAN interface. fields names the members that carry a
 * value (CANHUB_SETTING_*): in a request, what to change, all applied in one
 * step; in a reply, what the device reports. Sample points are in tenths of a
 * percent (875 = 87.5%). ctrlmode_mask names the CANHUB_CTRLMODE_* modes to
 * change and ctrlmode_flags their new values. termination is in ohms, 0 =
 * off. can_state is only reported.
 */
typedef struct {
    uint32_t struct_size;
    uint32_t fields;
    uint32_t bitrate;
    uint32_t data_bitrate;
    uint16_t sample_point;
    uint16_t data_sample_point;
    uint32_t ctrlmode_mask;
    uint32_t ctrlmode_flags;
    uint32_t restart_ms;
    uint32_t txqueuelen;
    uint8_t link_up;
    uint8_t can_state;
    uint16_t termination;
    CanHubBitTiming bit_timing;
    CanHubBitTiming data_bit_timing;
} CanHubInterfaceSettings;

/*
 * Transactions (see doc/protocol.md "Transactions"): the agent runs a sequence
 * of steps at the bus. A step transmits a frame, unless it is expect-only, and
 * waits as its template says. The roster lists the expected answering origins,
 * extracted as (X & origin_mask) >> origin_shift from the CAN id or from the 4
 * payload bytes at origin_payload_offset. Patterns match a frame when
 * (can_id & can_mask) == (pattern can_id & can_mask) and its first 8 payload
 * bytes, little-endian, match payload_value under payload_mask; a pattern
 * counts only if its bit (1 << CANHUB_TXN_PATTERN_*) is in patterns_present.
 */
typedef struct {
    uint32_t struct_size;
    uint8_t flags;
    uint8_t origin_source;
    uint8_t origin_shift;
    uint8_t origin_payload_offset;
    uint32_t origin_mask;
    uint32_t roster_count;
    uint32_t roster[CANHUB_TXN_ROSTER_MAX];
} CanHubTxnBegin;

typedef struct {
    uint32_t can_id;
    uint32_t can_mask;
    uint64_t payload_mask;
    uint64_t payload_value;
} CanHubTxnPattern;

/*
 * ISO-TP parameters of a template of type CANHUB_TXN_TYPE_ISOTP: each of its
 * steps is one ISO 15765-2 message the agent segments, and the patterns match
 * the reassembled answer arriving on response_id (doc/protocol.md "ISO-TP
 * steps"). With the address byte flag every frame starts with tx_address and
 * answers must start with rx_address (extended or mixed addressing).
 */
typedef struct {
    uint32_t response_id;
    uint8_t flags;
    uint8_t tx_address;
    uint8_t rx_address;
    uint8_t padding;
    uint8_t tx_data_length;
    uint8_t block_size;
    uint8_t st_min;
    uint8_t reserved;
} CanHubTxnIsotp;

typedef struct {
    uint32_t struct_size;
    uint8_t template_id;
    uint8_t advance_mode;
    uint8_t flags;
    uint8_t count_k;
    uint8_t patterns_present;
    uint8_t type;
    uint8_t reserved[2];
    uint32_t timeout_ms;
    uint32_t gap_us;
    uint32_t extend_timeout_ms;
    CanHubTxnPattern patterns[CANHUB_TXN_PATTERNS];
    CanHubTxnIsotp isotp;
} CanHubTxnTemplate;

typedef struct {
    uint32_t can_id;
    uint8_t template_id;
    uint8_t flags;
    uint8_t frame_flags;
    uint8_t length;
    uint64_t expect_value;
    uint8_t payload[CANHUB_FRAME_PAYLOAD_MAX];
} CanHubTxnStep;

/*
 * One step of an ISO-TP template: the message (1-4095 bytes) the agent sends to
 * can_id, which is also where it sends flow control for a multi-frame answer.
 */
typedef struct {
    uint32_t struct_size;
    uint32_t can_id;
    uint8_t template_id;
    uint8_t flags;
    uint16_t length;
    uint64_t expect_value;
    const uint8_t *data;
} CanHubTxnMessage;

typedef struct {
    uint32_t struct_size;
    uint32_t sent_steps;
    uint32_t completed_steps;
    uint32_t bytes_in_flight;
    uint32_t window_bytes;
} CanHubTxnProgress;

typedef struct {
    uint32_t struct_size;
    uint8_t state;
    uint8_t reason;
    uint8_t has_rx_frame;
    uint8_t reserved;
    uint32_t failing_seq;
    uint32_t completed_steps;
    uint32_t offending_origin;
    uint64_t seen_bitmap;
    uint64_t expected;
    uint64_t got;
    CanHubFrame rx_frame;
} CanHubTxnResult;

/*
 * The outcome of canhub_txn_run_script: the agent's result, how many steps the
 * script holds, its roster (the list of a template for) with the name of the
 * loop variable, so a report can name who did not answer, and the line of a
 * script error (0 when the script was fine).
 */
typedef struct {
    uint32_t struct_size;
    CanHubTxnResult result;
    uint32_t total_steps;
    uint32_t error_line;
    uint32_t roster_count;
    uint32_t roster[CANHUB_TXN_ROSTER_MAX];
    char roster_name[CANHUB_SCRIPT_NAME_MAX];
} CanHubScriptOutcome;

/*
 * A UDS conversation (ISO 14229) over ISO-TP steps with one ECU: where requests
 * go, the ISO-TP transport (its response_id is where the ECU answers), how
 * long a request waits for its answer (P2) and, after a response pending,
 * how long again (P2*). flags takes CANHUB_TXN_FLAG_EXCLUSIVE.
 */
typedef struct {
    uint32_t struct_size;
    uint32_t request_id;
    uint32_t p2_ms;
    uint32_t p2_extended_ms;
    uint8_t flags;
    uint8_t reserved[3];
    CanHubTxnIsotp transport;
} CanHubUds;

typedef int32_t (*CanHubScriptProgress)(void *context, const CanHubTxnProgress *progress, uint32_t total_steps);

CANHUB_API uint32_t canhub_api_version(void);

CANHUB_API CanHubSession *canhub_connect(const CanHubConnectConfig *config);
CANHUB_API void canhub_close(CanHubSession *session);
CANHUB_API const char *canhub_last_error(const CanHubSession *session);

CANHUB_API int32_t canhub_list(
    CanHubSession *session,
    CanHubInterfaceInfo *interfaces,
    size_t interfaces_max,
    int32_t timeout_ms
);
CANHUB_API int32_t canhub_open(CanHubSession *session, const char *interface_name, uint32_t flags, int32_t timeout_ms);
CANHUB_API int32_t canhub_set_filters(CanHubSession *session, const CanHubFilter *filters, uint8_t filter_count);
CANHUB_API int32_t canhub_recv(CanHubSession *session, CanHubFrame *frame, int32_t timeout_ms);
CANHUB_API int32_t canhub_send(CanHubSession *session, const CanHubFrame *frame);
CANHUB_API int32_t canhub_stats(CanHubSession *session, CanHubStats *stats);

/*
 * Reconfigure the interface named agent/iface (the hub needs an rwa grant for
 * this client) and read back the settings the device holds afterwards into
 * effective, which may be NULL. canhub_query only reads them. Both work with
 * or without an open interface. On failure canhub_last_error carries why.
 */
CANHUB_API int32_t canhub_configure(
    CanHubSession *session,
    const char *interface_name,
    const CanHubInterfaceSettings *requested,
    CanHubInterfaceSettings *effective,
    int32_t timeout_ms
);
CANHUB_API int32_t canhub_query(
    CanHubSession *session,
    const char *interface_name,
    CanHubInterfaceSettings *effective,
    int32_t timeout_ms
);

/*
 * Run a transaction on the open interface, which must be opened with
 * CANHUB_OPEN_FLAG_RELIABLE and CANHUB_OPEN_FLAG_WRITE. canhub_txn_begin waits
 * for the agent's grant. canhub_txn_send behaves like a blocking socket write:
 * it hands the steps over as the agent's window allows, waiting for credit,
 * and returns how many it took (fewer than count on timeout); frames received
 * meanwhile stay queued for canhub_recv. The last step handed over is held
 * back until canhub_txn_end, which marks it the last one. Steps of an ISO-TP
 * template go through canhub_txn_send_message instead (begin with
 * CANHUB_TXN_FLAG_ISOTP): it waits until the agent's window takes the whole
 * message, then hands it over, so a timeout leaves nothing half sent.
 * canhub_txn_wait returns CANHUB_OK once the transaction is done,
 * CANHUB_ERR_TXN_ABORTED with the report in result otherwise.
 * canhub_txn_abort cancels it.
 */
CANHUB_API int32_t canhub_txn_begin(CanHubSession *session, const CanHubTxnBegin *begin, int32_t timeout_ms);
CANHUB_API int32_t canhub_txn_template(CanHubSession *session, const CanHubTxnTemplate *template_definition, int32_t timeout_ms);
CANHUB_API int32_t canhub_txn_send(CanHubSession *session, const CanHubTxnStep *steps, uint32_t count, int32_t timeout_ms);
CANHUB_API int32_t canhub_txn_send_message(CanHubSession *session, const CanHubTxnMessage *message, int32_t timeout_ms);
CANHUB_API int32_t canhub_txn_end(CanHubSession *session, int32_t timeout_ms);
CANHUB_API int32_t canhub_txn_wait(CanHubSession *session, CanHubTxnResult *result, int32_t timeout_ms);
CANHUB_API int32_t canhub_txn_progress(CanHubSession *session, CanHubTxnProgress *progress);
CANHUB_API int32_t canhub_txn_abort(CanHubSession *session);

/*
 * UDS requests run at the agent as one transaction, the answers checked by the
 * UDS rules: a positive answer starts with the service id + 0x40 (or with
 * expect, which must), a negative one (7F <service> <code>) aborts the
 * transaction, 7F <service> 78 makes the request wait p2_extended_ms. A
 * request whose sub-function carries the suppress-positive-response bit does
 * not wait. canhub_uds_begin begins the transaction; canhub_uds_transfer sends
 * data as TransferData blocks (36 <counter> <block>, the counter from 1,
 * wrapping after 255), each answered 76 <counter>. End and wait with
 * canhub_txn_end and canhub_txn_wait; canhub_uds_negative_code reads the code
 * of a negative answer from the result (0 when the abort was something else).
 */
CANHUB_API int32_t canhub_uds_begin(CanHubSession *session, const CanHubUds *uds, int32_t timeout_ms);
CANHUB_API int32_t canhub_uds_request(
    CanHubSession *session,
    const CanHubUds *uds,
    const uint8_t *request,
    uint16_t length,
    const uint8_t *expect,
    uint8_t expect_length,
    int32_t timeout_ms
);
CANHUB_API int32_t canhub_uds_transfer(
    CanHubSession *session,
    const CanHubUds *uds,
    const uint8_t *data,
    uint32_t length,
    uint16_t block_size,
    int32_t timeout_ms
);
CANHUB_API uint8_t canhub_uds_negative_code(const CanHubTxnResult *result);

/*
 * Run a transaction script (doc/transaction-scripts.md) on the open interface,
 * opened as for canhub_txn_begin. Files the script reads resolve against the
 * script's directory. on_progress, which may be NULL, is called a few times a
 * second; returning 0 cancels the transaction. Returns CANHUB_OK when done,
 * CANHUB_ERR_TXN_ABORTED with the report in outcome, CANHUB_ERR_ARGUMENT for a
 * script error (canhub_last_error says "file:line: reason").
 */
CANHUB_API int32_t canhub_txn_run_script(
    CanHubSession *session,
    const char *script_path,
    CanHubScriptOutcome *outcome,
    CanHubScriptProgress on_progress,
    void *context
);

#ifdef __cplusplus
}
#endif

#endif
