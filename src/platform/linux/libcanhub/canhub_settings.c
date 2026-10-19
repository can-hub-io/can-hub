#include "platform/linux/libcanhub/canhub_settings.h"

#include <assert.h>
#include <string.h>

static_assert(CANHUB_SETTING_BITRATE == INTERFACE_FIELD_BITRATE, "public setting bits are the wire bits");
static_assert(CANHUB_SETTING_SAMPLE_POINT == INTERFACE_FIELD_SAMPLE_POINT, "public setting bits are the wire bits");
static_assert(CANHUB_SETTING_DATA_BITRATE == INTERFACE_FIELD_DATA_BITRATE, "public setting bits are the wire bits");
static_assert(CANHUB_SETTING_DATA_SAMPLE_POINT == INTERFACE_FIELD_DATA_SAMPLE_POINT, "public setting bits are the wire bits");
static_assert(CANHUB_SETTING_CTRLMODE == INTERFACE_FIELD_CTRLMODE, "public setting bits are the wire bits");
static_assert(CANHUB_SETTING_RESTART_MS == INTERFACE_FIELD_RESTART_MS, "public setting bits are the wire bits");
static_assert(CANHUB_SETTING_TXQUEUELEN == INTERFACE_FIELD_TXQUEUELEN, "public setting bits are the wire bits");
static_assert(CANHUB_SETTING_LINK == INTERFACE_FIELD_LINK, "public setting bits are the wire bits");
static_assert(CANHUB_SETTING_STATE == INTERFACE_FIELD_STATE, "public setting bits are the wire bits");
static_assert(CANHUB_CTRLMODE_LOOPBACK == INTERFACE_CTRLMODE_LOOPBACK, "public ctrlmode bits are the wire bits");
static_assert(CANHUB_CTRLMODE_LISTEN_ONLY == INTERFACE_CTRLMODE_LISTEN_ONLY, "public ctrlmode bits are the wire bits");
static_assert(CANHUB_CTRLMODE_TRIPLE_SAMPLING == INTERFACE_CTRLMODE_TRIPLE_SAMPLING, "public ctrlmode bits are the wire bits");
static_assert(CANHUB_CTRLMODE_ONE_SHOT == INTERFACE_CTRLMODE_ONE_SHOT, "public ctrlmode bits are the wire bits");
static_assert(CANHUB_CTRLMODE_BERR_REPORTING == INTERFACE_CTRLMODE_BERR_REPORTING, "public ctrlmode bits are the wire bits");
static_assert(CANHUB_CTRLMODE_FD == INTERFACE_CTRLMODE_FD, "public ctrlmode bits are the wire bits");
static_assert(CANHUB_STATE_SLEEPING == INTERFACE_STATE_SLEEPING, "public states are the wire states");

/* ---------- public ---------- */

void CanHubSettings_ToWire(const CanHubInterfaceSettings *public_settings, InterfaceSettings *settings)
{
    memset(settings, 0, sizeof(*settings));
    settings->fields = public_settings->fields & INTERFACE_FIELDS_SETTABLE;
    settings->bitrate = public_settings->bitrate;
    settings->data_bitrate = public_settings->data_bitrate;
    settings->sample_point = public_settings->sample_point;
    settings->data_sample_point = public_settings->data_sample_point;
    settings->ctrlmode_mask = public_settings->ctrlmode_mask;
    settings->ctrlmode_flags = public_settings->ctrlmode_flags;
    settings->restart_ms = public_settings->restart_ms;
    settings->txqueuelen = public_settings->txqueuelen;
    settings->link_up = public_settings->link_up != 0;
}

void CanHubSettings_FromWire(const InterfaceSettings *settings, CanHubInterfaceSettings *public_settings)
{
    uint32_t struct_size = public_settings->struct_size;

    memset(public_settings, 0, sizeof(*public_settings));
    public_settings->struct_size = struct_size;
    public_settings->fields = settings->fields;
    public_settings->bitrate = settings->bitrate;
    public_settings->data_bitrate = settings->data_bitrate;
    public_settings->sample_point = settings->sample_point;
    public_settings->data_sample_point = settings->data_sample_point;
    public_settings->ctrlmode_mask = settings->ctrlmode_mask;
    public_settings->ctrlmode_flags = settings->ctrlmode_flags;
    public_settings->restart_ms = settings->restart_ms;
    public_settings->txqueuelen = settings->txqueuelen;
    public_settings->link_up = settings->link_up ? 1 : 0;
    public_settings->can_state = settings->can_state;
}

int32_t CanHubSettings_Result(uint8_t configure_status)
{
    if (configure_status == CONFIGURE_STATUS_OK) {
        return CANHUB_OK;
    }
    if (configure_status == CONFIGURE_STATUS_UNKNOWN_INTERFACE) {
        return CANHUB_ERR_NOT_FOUND;
    }
    if (configure_status == CONFIGURE_STATUS_AGENT_UNREACHABLE) {
        return CANHUB_ERR_AGENT_UNREACHABLE;
    }
    if (configure_status == CONFIGURE_STATUS_DENIED) {
        return CANHUB_ERR_CONFIGURE_DENIED;
    }

    return CANHUB_ERR_APPLY_FAILED;
}
