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
    char interface[CANHUB_INTERFACE_NAME_MAX];
} CanHubInterfaceInfo;

typedef struct {
    uint32_t can_id;
    uint32_t can_mask;
} CanHubFilter;

typedef struct {
    uint32_t struct_size;
    const char *url;
    const char *state_directory;
    const char *certificate_path;
    const char *key_path;
    const char *hub_fingerprint;
    int32_t connect_timeout_ms;
} CanHubConnectConfig;

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
CANHUB_API int32_t canhub_open(CanHubSession *session, const char *interface, uint32_t flags, int32_t timeout_ms);
CANHUB_API int32_t canhub_set_filters(CanHubSession *session, const CanHubFilter *filters, uint8_t filter_count);
CANHUB_API int32_t canhub_recv(CanHubSession *session, CanHubFrame *frame, int32_t timeout_ms);
CANHUB_API int32_t canhub_send(CanHubSession *session, const CanHubFrame *frame);

#ifdef __cplusplus
}
#endif

#endif
