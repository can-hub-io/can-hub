#include "protocol/ifconfig_message.h"

#include <string.h>

#include "protocol/message_header.h"
#include "protocol/wire.h"

#define SETTINGS_FIELDS_OFFSET 0
#define SETTINGS_BITRATE_OFFSET 4
#define SETTINGS_DATA_BITRATE_OFFSET 8
#define SETTINGS_SAMPLE_POINT_OFFSET 12
#define SETTINGS_DATA_SAMPLE_POINT_OFFSET 14
#define SETTINGS_CTRLMODE_MASK_OFFSET 16
#define SETTINGS_CTRLMODE_FLAGS_OFFSET 20
#define SETTINGS_RESTART_MS_OFFSET 24
#define SETTINGS_TXQUEUELEN_OFFSET 28
#define SETTINGS_LINK_UP_OFFSET 32
#define SETTINGS_CAN_STATE_OFFSET 33
#define SETTINGS_TERMINATION_OFFSET 34
#define SETTINGS_BIT_TIMING_OFFSET 36
#define SETTINGS_DATA_BIT_TIMING_OFFSET 60
#define BIT_TIMING_TQ_OFFSET 0
#define BIT_TIMING_PROP_SEG_OFFSET 4
#define BIT_TIMING_PHASE_SEG1_OFFSET 8
#define BIT_TIMING_PHASE_SEG2_OFFSET 12
#define BIT_TIMING_SJW_OFFSET 16
#define BIT_TIMING_BRP_OFFSET 20
#define IFCONFIG_REQUEST_ID_OFFSET 0
#define IFCONFIG_INTERFACE_NAME_OFFSET 4
#define IFCONFIG_OP_OFFSET 20
#define IFCONFIG_SETTINGS_OFFSET 24
#define IFCONFIG_REPLY_REQUEST_ID_OFFSET 0
#define IFCONFIG_REPLY_STATUS_OFFSET 4
#define IFCONFIG_REPLY_SETTINGS_OFFSET 8
#define IFCONFIG_REPLY_DETAIL_OFFSET (IFCONFIG_REPLY_SETTINGS_OFFSET + INTERFACE_SETTINGS_SIZE)
#define CONFIGURE_AGENT_NAME_OFFSET 0
#define CONFIGURE_INTERFACE_NAME_OFFSET 128
#define CONFIGURE_OP_OFFSET 144
#define CONFIGURE_SETTINGS_OFFSET 148
#define CONFIGURE_REPLY_STATUS_OFFSET 0
#define CONFIGURE_REPLY_SETTINGS_OFFSET 4
#define CONFIGURE_REPLY_DETAIL_OFFSET (CONFIGURE_REPLY_SETTINGS_OFFSET + INTERFACE_SETTINGS_SIZE)

static size_t encode(uint8_t type, uint16_t body_size, uint8_t *buffer, size_t buffer_size, uint8_t **body);
static void writeSettings(uint8_t *destination, const InterfaceSettings *settings);
static bool readSettings(InterfaceSettings *settings, const uint8_t *source);
static void writeBitTiming(uint8_t *destination, const InterfaceBitTiming *timing);
static void readBitTiming(InterfaceBitTiming *timing, const uint8_t *source);
static bool namesRawTiming(const InterfaceBitTiming *timing);
static void writeText(uint8_t *destination, const char *text, size_t size);
static void copyName(char *destination, const uint8_t *source, size_t size);

/* ---------- public ---------- */

bool InterfaceSettings_HasTimingConflict(const InterfaceSettings *self)
{
    bool nominal_raw = (self->fields & INTERFACE_FIELD_BIT_TIMING) && namesRawTiming(&self->bit_timing);
    bool data_raw = (self->fields & INTERFACE_FIELD_DATA_BIT_TIMING) && namesRawTiming(&self->data_bit_timing);

    if (nominal_raw && (self->fields & INTERFACE_FIELDS_NOMINAL_RATE)) {
        return true;
    }

    return data_raw && (self->fields & INTERFACE_FIELDS_DATA_RATE);
}

const char *ConfigureStatus_Name(uint8_t status)
{
    if (status == CONFIGURE_STATUS_OK) {
        return "ok";
    }
    if (status == CONFIGURE_STATUS_UNKNOWN_INTERFACE) {
        return "unknown interface";
    }
    if (status == CONFIGURE_STATUS_AGENT_UNREACHABLE) {
        return "agent unreachable";
    }
    if (status == CONFIGURE_STATUS_DENIED) {
        return "not allowed to configure this interface";
    }

    return "apply failed";
}

size_t IfconfigMessage_Encode(const IfconfigMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;

    if (self->interface_name[REGISTER_INTERFACE_NAME_SIZE - 1] != '\0') {
        return 0;
    }
    if (encode(kMESSAGE_TYPE_IFCONFIG, IFCONFIG_BODY_SIZE, buffer, buffer_size, &body) == 0) {
        return 0;
    }

    Wire_WriteU32(body + IFCONFIG_REQUEST_ID_OFFSET, self->request_id);
    memcpy(body + IFCONFIG_INTERFACE_NAME_OFFSET, self->interface_name, strlen(self->interface_name));
    body[IFCONFIG_OP_OFFSET] = self->op;
    writeSettings(body + IFCONFIG_SETTINGS_OFFSET, &self->settings);

    return MESSAGE_HEADER_SIZE + IFCONFIG_BODY_SIZE;
}

bool IfconfigMessage_Decode(IfconfigMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < IFCONFIG_BODY_SIZE) {
        return false;
    }

    self->request_id = Wire_ReadU32(payload + IFCONFIG_REQUEST_ID_OFFSET);
    copyName(self->interface_name, payload + IFCONFIG_INTERFACE_NAME_OFFSET, REGISTER_INTERFACE_NAME_SIZE);
    self->op = payload[IFCONFIG_OP_OFFSET];

    return readSettings(&self->settings, payload + IFCONFIG_SETTINGS_OFFSET);
}

size_t IfconfigReplyMessage_Encode(const IfconfigReplyMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;

    if (encode(kMESSAGE_TYPE_IFCONFIG_REPLY, IFCONFIG_REPLY_BODY_SIZE, buffer, buffer_size, &body) == 0) {
        return 0;
    }

    Wire_WriteU32(body + IFCONFIG_REPLY_REQUEST_ID_OFFSET, self->request_id);
    body[IFCONFIG_REPLY_STATUS_OFFSET] = self->status;
    writeSettings(body + IFCONFIG_REPLY_SETTINGS_OFFSET, &self->settings);
    writeText(body + IFCONFIG_REPLY_DETAIL_OFFSET, self->detail, IFCONFIG_DETAIL_SIZE);

    return MESSAGE_HEADER_SIZE + IFCONFIG_REPLY_BODY_SIZE;
}

bool IfconfigReplyMessage_Decode(IfconfigReplyMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < IFCONFIG_REPLY_BODY_SIZE) {
        return false;
    }

    self->request_id = Wire_ReadU32(payload + IFCONFIG_REPLY_REQUEST_ID_OFFSET);
    self->status = payload[IFCONFIG_REPLY_STATUS_OFFSET];
    copyName(self->detail, payload + IFCONFIG_REPLY_DETAIL_OFFSET, IFCONFIG_DETAIL_SIZE);

    return readSettings(&self->settings, payload + IFCONFIG_REPLY_SETTINGS_OFFSET);
}

size_t ConfigureMessage_Encode(const ConfigureMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;

    if (self->agent_name[REGISTER_AGENT_NAME_SIZE - 1] != '\0') {
        return 0;
    }
    if (self->interface_name[REGISTER_INTERFACE_NAME_SIZE - 1] != '\0') {
        return 0;
    }
    if (encode(kMESSAGE_TYPE_CONFIGURE, CONFIGURE_BODY_SIZE, buffer, buffer_size, &body) == 0) {
        return 0;
    }

    memcpy(body + CONFIGURE_AGENT_NAME_OFFSET, self->agent_name, strlen(self->agent_name));
    memcpy(body + CONFIGURE_INTERFACE_NAME_OFFSET, self->interface_name, strlen(self->interface_name));
    body[CONFIGURE_OP_OFFSET] = self->op;
    writeSettings(body + CONFIGURE_SETTINGS_OFFSET, &self->settings);

    return MESSAGE_HEADER_SIZE + CONFIGURE_BODY_SIZE;
}

bool ConfigureMessage_Decode(ConfigureMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < CONFIGURE_BODY_SIZE) {
        return false;
    }

    copyName(self->agent_name, payload + CONFIGURE_AGENT_NAME_OFFSET, REGISTER_AGENT_NAME_SIZE);
    copyName(self->interface_name, payload + CONFIGURE_INTERFACE_NAME_OFFSET, REGISTER_INTERFACE_NAME_SIZE);
    self->op = payload[CONFIGURE_OP_OFFSET];

    return readSettings(&self->settings, payload + CONFIGURE_SETTINGS_OFFSET);
}

size_t ConfigureReplyMessage_Encode(const ConfigureReplyMessage *self, uint8_t *buffer, size_t buffer_size)
{
    uint8_t *body;

    if (encode(kMESSAGE_TYPE_CONFIGURE_REPLY, CONFIGURE_REPLY_BODY_SIZE, buffer, buffer_size, &body) == 0) {
        return 0;
    }

    body[CONFIGURE_REPLY_STATUS_OFFSET] = self->status;
    writeSettings(body + CONFIGURE_REPLY_SETTINGS_OFFSET, &self->settings);
    writeText(body + CONFIGURE_REPLY_DETAIL_OFFSET, self->detail, IFCONFIG_DETAIL_SIZE);

    return MESSAGE_HEADER_SIZE + CONFIGURE_REPLY_BODY_SIZE;
}

bool ConfigureReplyMessage_Decode(ConfigureReplyMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < CONFIGURE_REPLY_BODY_SIZE) {
        return false;
    }

    self->status = payload[CONFIGURE_REPLY_STATUS_OFFSET];
    copyName(self->detail, payload + CONFIGURE_REPLY_DETAIL_OFFSET, IFCONFIG_DETAIL_SIZE);

    return readSettings(&self->settings, payload + CONFIGURE_REPLY_SETTINGS_OFFSET);
}

/* ---------- private ---------- */

static size_t encode(uint8_t type, uint16_t body_size, uint8_t *buffer, size_t buffer_size, uint8_t **body)
{
    MessageHeader header;
    size_t total_size = MESSAGE_HEADER_SIZE + body_size;

    if (buffer_size < total_size) {
        return 0;
    }

    header.type = type;
    header.flags = 0;
    header.length = body_size;
    MessageHeader_Encode(&header, buffer, buffer_size);

    *body = buffer + MESSAGE_HEADER_SIZE;
    memset(*body, 0, body_size);

    return total_size;
}

static void writeSettings(uint8_t *destination, const InterfaceSettings *settings)
{
    Wire_WriteU32(destination + SETTINGS_FIELDS_OFFSET, settings->fields);
    Wire_WriteU32(destination + SETTINGS_BITRATE_OFFSET, settings->bitrate);
    Wire_WriteU32(destination + SETTINGS_DATA_BITRATE_OFFSET, settings->data_bitrate);
    Wire_WriteU16(destination + SETTINGS_SAMPLE_POINT_OFFSET, settings->sample_point);
    Wire_WriteU16(destination + SETTINGS_DATA_SAMPLE_POINT_OFFSET, settings->data_sample_point);
    Wire_WriteU32(destination + SETTINGS_CTRLMODE_MASK_OFFSET, settings->ctrlmode_mask);
    Wire_WriteU32(destination + SETTINGS_CTRLMODE_FLAGS_OFFSET, settings->ctrlmode_flags);
    Wire_WriteU32(destination + SETTINGS_RESTART_MS_OFFSET, settings->restart_ms);
    Wire_WriteU32(destination + SETTINGS_TXQUEUELEN_OFFSET, settings->txqueuelen);
    destination[SETTINGS_LINK_UP_OFFSET] = settings->link_up ? 1 : 0;
    destination[SETTINGS_CAN_STATE_OFFSET] = settings->can_state;
    Wire_WriteU16(destination + SETTINGS_TERMINATION_OFFSET, settings->termination);
    writeBitTiming(destination + SETTINGS_BIT_TIMING_OFFSET, &settings->bit_timing);
    writeBitTiming(destination + SETTINGS_DATA_BIT_TIMING_OFFSET, &settings->data_bit_timing);
}

static bool readSettings(InterfaceSettings *settings, const uint8_t *source)
{
    settings->fields = Wire_ReadU32(source + SETTINGS_FIELDS_OFFSET);
    settings->bitrate = Wire_ReadU32(source + SETTINGS_BITRATE_OFFSET);
    settings->data_bitrate = Wire_ReadU32(source + SETTINGS_DATA_BITRATE_OFFSET);
    settings->sample_point = Wire_ReadU16(source + SETTINGS_SAMPLE_POINT_OFFSET);
    settings->data_sample_point = Wire_ReadU16(source + SETTINGS_DATA_SAMPLE_POINT_OFFSET);
    settings->ctrlmode_mask = Wire_ReadU32(source + SETTINGS_CTRLMODE_MASK_OFFSET);
    settings->ctrlmode_flags = Wire_ReadU32(source + SETTINGS_CTRLMODE_FLAGS_OFFSET);
    settings->restart_ms = Wire_ReadU32(source + SETTINGS_RESTART_MS_OFFSET);
    settings->txqueuelen = Wire_ReadU32(source + SETTINGS_TXQUEUELEN_OFFSET);
    settings->link_up = source[SETTINGS_LINK_UP_OFFSET] != 0;
    settings->can_state = source[SETTINGS_CAN_STATE_OFFSET];
    settings->termination = Wire_ReadU16(source + SETTINGS_TERMINATION_OFFSET);
    readBitTiming(&settings->bit_timing, source + SETTINGS_BIT_TIMING_OFFSET);
    readBitTiming(&settings->data_bit_timing, source + SETTINGS_DATA_BIT_TIMING_OFFSET);

    if (settings->sample_point > INTERFACE_SAMPLE_POINT_MAX) {
        return false;
    }

    return settings->data_sample_point <= INTERFACE_SAMPLE_POINT_MAX;
}

static void writeBitTiming(uint8_t *destination, const InterfaceBitTiming *timing)
{
    Wire_WriteU32(destination + BIT_TIMING_TQ_OFFSET, timing->tq);
    Wire_WriteU32(destination + BIT_TIMING_PROP_SEG_OFFSET, timing->prop_seg);
    Wire_WriteU32(destination + BIT_TIMING_PHASE_SEG1_OFFSET, timing->phase_seg1);
    Wire_WriteU32(destination + BIT_TIMING_PHASE_SEG2_OFFSET, timing->phase_seg2);
    Wire_WriteU32(destination + BIT_TIMING_SJW_OFFSET, timing->sjw);
    Wire_WriteU32(destination + BIT_TIMING_BRP_OFFSET, timing->brp);
}

static void readBitTiming(InterfaceBitTiming *timing, const uint8_t *source)
{
    timing->tq = Wire_ReadU32(source + BIT_TIMING_TQ_OFFSET);
    timing->prop_seg = Wire_ReadU32(source + BIT_TIMING_PROP_SEG_OFFSET);
    timing->phase_seg1 = Wire_ReadU32(source + BIT_TIMING_PHASE_SEG1_OFFSET);
    timing->phase_seg2 = Wire_ReadU32(source + BIT_TIMING_PHASE_SEG2_OFFSET);
    timing->sjw = Wire_ReadU32(source + BIT_TIMING_SJW_OFFSET);
    timing->brp = Wire_ReadU32(source + BIT_TIMING_BRP_OFFSET);
}

static bool namesRawTiming(const InterfaceBitTiming *timing)
{
    return timing->tq != 0 || timing->brp != 0;
}

static void writeText(uint8_t *destination, const char *text, size_t size)
{
    size_t length = strnlen(text, size - 1);

    memcpy(destination, text, length);
}

static void copyName(char *destination, const uint8_t *source, size_t size)
{
    memcpy(destination, source, size);
    destination[size - 1] = '\0';
}
