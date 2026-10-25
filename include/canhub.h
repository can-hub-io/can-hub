#ifndef CANHUB_H
#define CANHUB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CANHUB_API_VERSION 2

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

#define CANHUB_STANDARD_ID_MASK 0x7FFu
#define CANHUB_EXTENDED_ID_MASK 0x1FFFFFFFu

#define CANHUB_FRAME_FLAG_FD (1u << 0)
#define CANHUB_FRAME_FLAG_BRS (1u << 1)
#define CANHUB_FRAME_FLAG_RTR (1u << 2)
/* Received only, with CANHUB_OPEN_FLAG_ERROR_FRAMES: a controller error frame. */
#define CANHUB_FRAME_FLAG_ERROR (1u << 3)
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
#define CANHUB_TXN_MATCH_BYTES 8
#define CANHUB_ISOTP_MESSAGE_MAX 4095

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

/*
 * A CAN identifier: value is 11 bits for a standard id, 29 for an extended
 * one. A standard and an extended id never match each other, whatever their
 * value.
 */
typedef struct {
    uint32_t value;
    bool extended;
} CanHubCanId;

#define CANHUB_STANDARD_ID(id_value) ((CanHubCanId){ (id_value), false })
#define CANHUB_EXTENDED_ID(id_value) ((CanHubCanId){ (id_value), true })

typedef struct {
    uint64_t timestamp_us;
    CanHubCanId id;
    uint8_t flags;
    uint8_t length;
    uint8_t payload[CANHUB_FRAME_PAYLOAD_MAX];
} CanHubFrame;

typedef struct {
    uint32_t interface_id;
    char agent[CANHUB_AGENT_NAME_MAX];
    char interface_name[CANHUB_INTERFACE_NAME_MAX];
} CanHubInterfaceInfo;

/*
 * Receive the frames whose id equals id, except in id_ignored_bits (0: the
 * exact id). No filter at all receives everything.
 */
typedef struct {
    CanHubCanId id;
    uint32_t id_ignored_bits;
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
 * Transactions (doc/libcanhub.md "Transactions", doc/protocol.md): the agent
 * runs a sequence of steps at the bus. A step sends a frame, or an ISO-TP
 * message over a link, and waits as its response says. Declarations are
 * structs, and every zero means what it says: id 0 is the id 0x000, a byte
 * whose mask is 0 is not compared; what is optional is a pointer, NULL when
 * absent.
 */
typedef struct CanHubTxn CanHubTxn;

typedef struct {
    uint8_t mask;
    uint8_t value;
} CanHubTxnByte;

#define CANHUB_TXN_EQUALS(byte_value) { 0xFF, (byte_value) }
#define CANHUB_TXN_MASKED(byte_mask, byte_value) { (byte_mask), (byte_value) }
#define CANHUB_TXN_BYTES(...) (const uint8_t[]){ __VA_ARGS__ }, sizeof((const uint8_t[]){ __VA_ARGS__ })

/*
 * A frame meets a match when its id equals id except in id_ignored_bits, and
 * each of its first 8 data bytes meets bytes[i] under bytes[i].mask.
 * CANHUB_TXN_ANY_ID_BITS ignores the id and its width: any frame, standard or
 * extended, meets the id part.
 */
#define CANHUB_TXN_ANY_ID_BITS 0xFFFFFFFFu

typedef struct {
    CanHubCanId id;
    uint32_t id_ignored_bits;
    CanHubTxnByte bytes[CANHUB_TXN_MATCH_BYTES];
} CanHubTxnMatch;

typedef enum {
    CANHUB_TXN_NODE_IN_ID = 0,
    CANHUB_TXN_NODE_IN_BYTE = 1
} CanHubTxnNodeSource;

/*
 * What a step waits for. success completes it, failure aborts the
 * transaction, pending (a "busy, wait" answer, checked first) restarts the
 * wait with timeout_after_pending_ms. With nodes, the step waits for an
 * answer from each of them (node_count - allowed_missing are enough); a node
 * is told by the bits success.id_ignored_bits leaves free, or by data byte
 * node_byte. continue_on_timeout moves on when the time runs out instead of
 * aborting; on_mismatch_abort aborts on a frame with the success id that does
 * not meet success.
 */
typedef struct {
    CanHubTxnMatch success;
    const CanHubTxnMatch *failure;
    const CanHubTxnMatch *pending;
    const uint32_t *nodes;
    uint32_t node_count;
    uint32_t allowed_missing;
    CanHubTxnNodeSource node_from;
    uint8_t node_byte;
    uint32_t timeout_ms;
    uint32_t timeout_after_pending_ms;
    bool continue_on_timeout;
    bool on_mismatch_abort;
} CanHubTxnResponse;

/*
 * Two ends of ISO 15765-2: the agent sends each message on tx_id, segmented,
 * and reassembles the answer arriving on rx_id. address_byte starts every
 * frame with tx_address and expects rx_address (extended or mixed
 * addressing); padded fills short frames with padding; fd sends CAN FD frames
 * of fd_data_length bytes; block_size and st_min (raw ISO 15765-2 value) go
 * in the flow control the agent answers with.
 */
typedef struct {
    CanHubCanId tx_id;
    CanHubCanId rx_id;
    bool address_byte;
    uint8_t tx_address;
    uint8_t rx_address;
    bool padded;
    uint8_t padding;
    bool fd;
    bool bitrate_switch;
    uint8_t fd_data_length;
    uint8_t block_size;
    uint8_t st_min;
} CanHubTxnIsotpLink;

typedef struct {
    CanHubCanId id;
    uint8_t data[CANHUB_FRAME_PAYLOAD_MAX];
    uint8_t length;
    uint32_t every_ms;
} CanHubTxnKeepalive;

/*
 * A step of a response with nodes that ended without every node: step is its
 * position in the transaction, missing the nodes that had not answered.
 */
typedef struct {
    uint32_t step;
    uint32_t missing[CANHUB_TXN_ROSTER_MAX];
    uint32_t missing_count;
} CanHubTxnRosterResult;

typedef void (*CanHubTxnRosterCallback)(void *context, const CanHubTxnRosterResult *result);

/*
 * exclusive keeps the other clients' frames off the interface while the
 * transaction runs. idle_timeout_ms (required) is how long the agent waits
 * for steps once it ran out of them, sending keepalive meanwhile.
 * bitrate_switch sends the CAN FD frames of the steps with bit rate switch.
 * on_roster is called from inside the transaction's calls, on their thread,
 * and must not call them.
 */
typedef struct {
    bool exclusive;
    uint32_t idle_timeout_ms;
    const CanHubTxnKeepalive *keepalive;
    bool bitrate_switch;
    CanHubTxnRosterCallback on_roster;
    void *context;
} CanHubTxnOptions;

/*
 * Every way of sending a step. data NULL sends nothing (wait for response,
 * where if any); data with length 0 sends an empty frame. More than 8 bytes
 * is a CAN FD frame. With isotp, data is a message (1-4095 bytes) and id must
 * be the link's tx_id. where adds to the response for this step only: its id
 * and id_ignored_bits replace the response's, its bytes add to them. gap_us
 * keeps at least that long before the next step's frame.
 */
typedef struct {
    CanHubCanId id;
    const uint8_t *data;
    uint16_t length;
    const CanHubTxnIsotpLink *isotp;
    const CanHubTxnResponse *response;
    const CanHubTxnMatch *where;
    uint32_t gap_us;
} CanHubTxnStep;

typedef struct {
    uint32_t sent_steps;
    uint32_t completed_steps;
    uint32_t bytes_in_flight;
    uint32_t window_bytes;
} CanHubTxnProgress;

typedef enum {
    CANHUB_TXN_REASON_NONE = 0,
    CANHUB_TXN_REASON_TIMEOUT = 1,
    CANHUB_TXN_REASON_FAILURE_MATCHED = 2,
    CANHUB_TXN_REASON_VALUE_MISMATCH = 3,
    CANHUB_TXN_REASON_TX_FAILED = 4,
    CANHUB_TXN_REASON_CANCELLED = 5,
    CANHUB_TXN_REASON_STARVED = 6,
    CANHUB_TXN_REASON_OVERRUN = 7,
    CANHUB_TXN_REASON_AGENT_DISCONNECTED = 8,
    CANHUB_TXN_REASON_INVALID_STEP = 9,
    CANHUB_TXN_REASON_FLOW_CONTROL_TIMEOUT = 10,
    CANHUB_TXN_REASON_CONSECUTIVE_FRAME_TIMEOUT = 11,
    CANHUB_TXN_REASON_OVERFLOW = 12,
    CANHUB_TXN_REASON_SEQUENCE_ERROR = 13,
    CANHUB_TXN_REASON_WAIT_LIMIT = 14,
    CANHUB_TXN_REASON_UNSUPPORTED = 15
} CanHubTxnReason;

/*
 * How the transaction ended. failing_roster names who had not answered when
 * the failing step waited for nodes (missing_count 0 otherwise); expected and
 * got are the 8 data bytes of a value mismatch; frame, when has_frame, the
 * frame that ended it, from offending_node for a response with nodes.
 */
typedef struct {
    bool done;
    CanHubTxnReason reason;
    uint32_t failing_step;
    uint32_t completed_steps;
    CanHubTxnRosterResult failing_roster;
    uint32_t offending_node;
    uint8_t expected[CANHUB_TXN_MATCH_BYTES];
    uint8_t got[CANHUB_TXN_MATCH_BYTES];
    bool has_frame;
    CanHubFrame frame;
} CanHubTxnReport;

/*
 * A CRC of the Rocksoft model (width 8-32 bits), as transaction scripts
 * define them.
 */
typedef struct {
    uint8_t width;
    uint32_t polynomial;
    uint32_t initial;
    bool reflect_input;
    bool reflect_output;
    uint32_t final_xor;
} CanHubTxnCrc;

typedef int32_t (*CanHubTxnProgressCallback)(void *context, const CanHubTxnProgress *progress, uint32_t total_steps);

/*
 * What a script run reports while it runs: on_roster as for a transaction,
 * on_progress a few times a second (returning 0 cancels the transaction).
 */
typedef struct {
    CanHubTxnRosterCallback on_roster;
    CanHubTxnProgressCallback on_progress;
    void *context;
} CanHubTxnScriptOptions;

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
 * Transactions run on the open interface, which must be opened with
 * CANHUB_OPEN_FLAG_RELIABLE and CANHUB_OPEN_FLAG_WRITE; one per session at a
 * time. canhub_txn_create sends nothing: the first step starts the
 * transaction at the agent, and every step waits for the agent's window like
 * a blocking write. A step the API refuses (canhub_last_error says why)
 * cancels the transaction; CANHUB_ERR_AGENT_UNREACHABLE when the agent did
 * not grant it within 5 s. canhub_txn_finish waits for the outcome, fills
 * report and frees the transaction: call it after every canhub_txn_create,
 * also after an error. It returns CANHUB_OK when the transaction is done,
 * CANHUB_ERR_TXN_ABORTED with the report otherwise. canhub_close cancels a
 * transaction still running at the agent; the transaction's calls then
 * return CANHUB_ERR_DISCONNECTED and canhub_txn_finish only frees it.
 * canhub_txn_abort may be called from another thread. For an event loop or a
 * binding, the timed twins give up with CANHUB_ERR_TIMEOUT: canhub_txn_try_step
 * when the agent has not granted the transaction yet or its window did not
 * take the step in time (nothing of it sent: call it again with the same
 * step), canhub_txn_wait when the outcome is not known yet (it ends the
 * transaction first; finish then reports at once). timeout_ms -1 waits
 * forever. sum and crc compute the checks scripts use.
 */
CANHUB_API CanHubTxn *canhub_txn_create(CanHubSession *session, const CanHubTxnOptions *options);
CANHUB_API int32_t canhub_txn_send(
    CanHubTxn *transaction,
    CanHubCanId id,
    const uint8_t *data,
    uint8_t length,
    const CanHubTxnResponse *response
);
CANHUB_API int32_t canhub_txn_send_message(
    CanHubTxn *transaction,
    const CanHubTxnIsotpLink *link,
    const uint8_t *data,
    uint16_t length,
    const CanHubTxnResponse *response
);
CANHUB_API int32_t canhub_txn_expect(CanHubTxn *transaction, const CanHubTxnResponse *response);
CANHUB_API int32_t canhub_txn_step(CanHubTxn *transaction, const CanHubTxnStep *step);
CANHUB_API int32_t canhub_txn_try_step(CanHubTxn *transaction, const CanHubTxnStep *step, int32_t timeout_ms);
CANHUB_API int32_t canhub_txn_progress(CanHubTxn *transaction, CanHubTxnProgress *progress);
CANHUB_API int32_t canhub_txn_abort(CanHubTxn *transaction);
CANHUB_API int32_t canhub_txn_wait(CanHubTxn *transaction, int32_t timeout_ms);
CANHUB_API int32_t canhub_txn_finish(CanHubTxn *transaction, CanHubTxnReport *report);
CANHUB_API const char *canhub_txn_reason_name(CanHubTxnReason reason);

/*
 * Run a transaction script (doc/transaction-scripts.md) on the open interface,
 * opened as for a transaction. canhub_txn_run_script takes the text, and
 * resolves the files it reads against base_directory (NULL: the working
 * directory); canhub_txn_run_script_file reads the script and resolves against
 * its directory. options may be NULL. Returns as canhub_txn_finish, or
 * CANHUB_ERR_ARGUMENT for a script error, canhub_last_error then saying
 * "script:LINE: reason" ("PATH:LINE: reason" for a file).
 */
CANHUB_API int32_t canhub_txn_run_script(
    CanHubSession *session,
    const char *text,
    const char *base_directory,
    const CanHubTxnScriptOptions *options,
    CanHubTxnReport *report
);
CANHUB_API int32_t canhub_txn_run_script_file(
    CanHubSession *session,
    const char *path,
    const CanHubTxnScriptOptions *options,
    CanHubTxnReport *report
);

CANHUB_API uint8_t canhub_txn_sum8(const uint8_t *data, uint32_t length);
CANHUB_API uint16_t canhub_txn_sum16(const uint8_t *data, uint32_t length);
CANHUB_API uint32_t canhub_txn_sum32(const uint8_t *data, uint32_t length);
CANHUB_API uint16_t canhub_txn_crc16(const uint8_t *data, uint32_t length);
CANHUB_API uint32_t canhub_txn_crc32(const uint8_t *data, uint32_t length);
CANHUB_API uint32_t canhub_txn_crc(const CanHubTxnCrc *crc, const uint8_t *data, uint32_t length);

#ifdef __cplusplus
}
#endif

#endif
