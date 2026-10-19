#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol/register_message.h"

#define INTERFACE_SETTINGS_SIZE 40
#define IFCONFIG_DETAIL_SIZE 64
#define IFCONFIG_BODY_SIZE 64
#define IFCONFIG_REPLY_BODY_SIZE 112
#define CONFIGURE_BODY_SIZE 188
#define CONFIGURE_REPLY_BODY_SIZE 108

#define INTERFACE_FIELD_BITRATE (1u << 0)
#define INTERFACE_FIELD_SAMPLE_POINT (1u << 1)
#define INTERFACE_FIELD_DATA_BITRATE (1u << 2)
#define INTERFACE_FIELD_DATA_SAMPLE_POINT (1u << 3)
#define INTERFACE_FIELD_CTRLMODE (1u << 4)
#define INTERFACE_FIELD_RESTART_MS (1u << 5)
#define INTERFACE_FIELD_TXQUEUELEN (1u << 6)
#define INTERFACE_FIELD_LINK (1u << 7)
#define INTERFACE_FIELD_STATE (1u << 8)
#define INTERFACE_FIELDS_SETTABLE (INTERFACE_FIELD_STATE - 1)
#define INTERFACE_FIELDS_NEED_LINK_DOWN (INTERFACE_FIELD_TXQUEUELEN - 1)

#define INTERFACE_CTRLMODE_LOOPBACK (1u << 0)
#define INTERFACE_CTRLMODE_LISTEN_ONLY (1u << 1)
#define INTERFACE_CTRLMODE_TRIPLE_SAMPLING (1u << 2)
#define INTERFACE_CTRLMODE_ONE_SHOT (1u << 3)
#define INTERFACE_CTRLMODE_BERR_REPORTING (1u << 4)
#define INTERFACE_CTRLMODE_FD (1u << 5)
#define INTERFACE_CTRLMODES_KNOWN ((INTERFACE_CTRLMODE_FD << 1) - 1)

#define INTERFACE_SAMPLE_POINT_MAX 1000

#define INTERFACE_STATE_ERROR_ACTIVE 0
#define INTERFACE_STATE_ERROR_WARNING 1
#define INTERFACE_STATE_ERROR_PASSIVE 2
#define INTERFACE_STATE_BUS_OFF 3
#define INTERFACE_STATE_STOPPED 4
#define INTERFACE_STATE_SLEEPING 5

#define IFCONFIG_OP_APPLY 0
#define IFCONFIG_OP_QUERY 1

#define IFCONFIG_STATUS_OK 0
#define IFCONFIG_STATUS_UNKNOWN_INTERFACE 1
#define IFCONFIG_STATUS_APPLY_FAILED 2

#define CONFIGURE_STATUS_OK 0
#define CONFIGURE_STATUS_UNKNOWN_INTERFACE 1
#define CONFIGURE_STATUS_AGENT_UNREACHABLE 2
#define CONFIGURE_STATUS_APPLY_FAILED 3
#define CONFIGURE_STATUS_DENIED 4

/*
 * The settings of one CAN interface. fields names the members that carry a
 * value: in a request, what to change; in a reply, what the device reports.
 * sample points are in tenths of a percent (875 = 87.5%). ctrlmode_mask names
 * the modes to change and ctrlmode_flags their new values. can_state is only
 * reported, never set.
 */
typedef struct {
    uint32_t fields;
    uint32_t bitrate;
    uint32_t data_bitrate;
    uint16_t sample_point;
    uint16_t data_sample_point;
    uint32_t ctrlmode_mask;
    uint32_t ctrlmode_flags;
    uint32_t restart_ms;
    uint32_t txqueuelen;
    bool link_up;
    uint8_t can_state;
} InterfaceSettings;

typedef struct {
    uint32_t request_id;
    char interface_name[REGISTER_INTERFACE_NAME_SIZE];
    uint8_t op;
    InterfaceSettings settings;
} IfconfigMessage;

typedef struct {
    uint32_t request_id;
    uint8_t status;
    InterfaceSettings settings;
    char detail[IFCONFIG_DETAIL_SIZE];
} IfconfigReplyMessage;

typedef struct {
    char agent_name[REGISTER_AGENT_NAME_SIZE];
    char interface_name[REGISTER_INTERFACE_NAME_SIZE];
    uint8_t op;
    InterfaceSettings settings;
} ConfigureMessage;

typedef struct {
    uint8_t status;
    InterfaceSettings settings;
    char detail[IFCONFIG_DETAIL_SIZE];
} ConfigureReplyMessage;

size_t IfconfigMessage_Encode(const IfconfigMessage *self, uint8_t *buffer, size_t buffer_size);
bool IfconfigMessage_Decode(IfconfigMessage *self, const uint8_t *payload, size_t payload_length);

size_t IfconfigReplyMessage_Encode(const IfconfigReplyMessage *self, uint8_t *buffer, size_t buffer_size);
bool IfconfigReplyMessage_Decode(IfconfigReplyMessage *self, const uint8_t *payload, size_t payload_length);

size_t ConfigureMessage_Encode(const ConfigureMessage *self, uint8_t *buffer, size_t buffer_size);
bool ConfigureMessage_Decode(ConfigureMessage *self, const uint8_t *payload, size_t payload_length);

size_t ConfigureReplyMessage_Encode(const ConfigureReplyMessage *self, uint8_t *buffer, size_t buffer_size);
bool ConfigureReplyMessage_Decode(ConfigureReplyMessage *self, const uint8_t *payload, size_t payload_length);
