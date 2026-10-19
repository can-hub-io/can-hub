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

#ifdef __cplusplus
}
#endif

#endif
