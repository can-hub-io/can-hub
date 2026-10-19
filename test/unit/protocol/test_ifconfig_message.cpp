#include <cest>

#include <cstdio>
#include <cstring>
#include <string>

extern "C" {
#include "protocol/ifconfig_message.h"
#include "protocol/message_header.h"
}

#define REQUEST_ID 0xA1B2C3D4u

static InterfaceSettings fdSettings(void);

describe("ifconfig_message", []() {
    it("round-trips the hub-to-agent request with every setting", []() {
        IfconfigMessage request;
        IfconfigMessage decoded;
        uint8_t buffer[MESSAGE_HEADER_SIZE + IFCONFIG_BODY_SIZE];
        size_t encoded_size;
        bool decoded_ok;

        memset(&request, 0, sizeof(request));
        request.request_id = REQUEST_ID;
        snprintf(request.interface_name, sizeof(request.interface_name), "can0");
        request.op = IFCONFIG_OP_APPLY;
        request.settings = fdSettings();

        encoded_size = IfconfigMessage_Encode(&request, buffer, sizeof(buffer));
        decoded_ok = IfconfigMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, IFCONFIG_BODY_SIZE);

        expect(encoded_size).toBe((size_t)(MESSAGE_HEADER_SIZE + IFCONFIG_BODY_SIZE));
        expect(decoded_ok).toBe(true);
        expect(decoded.request_id).toBe(REQUEST_ID);
        expect(std::string(decoded.interface_name)).toBe(std::string("can0"));
        expect(decoded.settings.fields).toBe(request.settings.fields);
        expect(decoded.settings.bitrate).toBe((uint32_t)500000);
        expect(decoded.settings.data_bitrate).toBe((uint32_t)2000000);
        expect(decoded.settings.sample_point).toBe((uint16_t)875);
        expect(decoded.settings.data_sample_point).toBe((uint16_t)750);
        expect(decoded.settings.ctrlmode_mask).toBe((uint32_t)(INTERFACE_CTRLMODE_FD | INTERFACE_CTRLMODE_LISTEN_ONLY));
        expect(decoded.settings.ctrlmode_flags).toBe((uint32_t)INTERFACE_CTRLMODE_FD);
        expect(decoded.settings.restart_ms).toBe((uint32_t)100);
        expect(decoded.settings.txqueuelen).toBe((uint32_t)1000);
        expect(decoded.settings.link_up).toBe(true);
    });

    it("rejects a sample point past one hundred percent", []() {
        IfconfigMessage request;
        IfconfigMessage decoded;
        uint8_t buffer[MESSAGE_HEADER_SIZE + IFCONFIG_BODY_SIZE];
        bool decoded_ok;

        memset(&request, 0, sizeof(request));
        request.settings.fields = INTERFACE_FIELD_SAMPLE_POINT;
        request.settings.sample_point = INTERFACE_SAMPLE_POINT_MAX + 1;

        IfconfigMessage_Encode(&request, buffer, sizeof(buffer));
        decoded_ok = IfconfigMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, IFCONFIG_BODY_SIZE);

        expect(decoded_ok).toBe(false);
    });

    it("round-trips the agent reply with the effective settings and the detail", []() {
        IfconfigReplyMessage reply;
        IfconfigReplyMessage decoded;
        uint8_t buffer[MESSAGE_HEADER_SIZE + IFCONFIG_REPLY_BODY_SIZE];
        size_t encoded_size;
        bool decoded_ok;

        memset(&reply, 0, sizeof(reply));
        reply.request_id = REQUEST_ID;
        reply.status = IFCONFIG_STATUS_APPLY_FAILED;
        reply.settings = fdSettings();
        reply.settings.fields |= INTERFACE_FIELD_STATE;
        reply.settings.can_state = INTERFACE_STATE_BUS_OFF;
        snprintf(reply.detail, sizeof(reply.detail), "Operation not supported");

        encoded_size = IfconfigReplyMessage_Encode(&reply, buffer, sizeof(buffer));
        decoded_ok = IfconfigReplyMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, IFCONFIG_REPLY_BODY_SIZE);

        expect(encoded_size).toBe((size_t)(MESSAGE_HEADER_SIZE + IFCONFIG_REPLY_BODY_SIZE));
        expect(decoded_ok).toBe(true);
        expect(decoded.request_id).toBe(REQUEST_ID);
        expect(decoded.status).toBe(IFCONFIG_STATUS_APPLY_FAILED);
        expect(decoded.settings.can_state).toBe((uint8_t)INTERFACE_STATE_BUS_OFF);
        expect(std::string(decoded.detail)).toBe(std::string("Operation not supported"));
    });

    it("round-trips a configure request with the namespaced name", []() {
        ConfigureMessage request;
        ConfigureMessage decoded;
        uint8_t buffer[MESSAGE_HEADER_SIZE + CONFIGURE_BODY_SIZE];
        size_t encoded_size;
        bool decoded_ok;

        memset(&request, 0, sizeof(request));
        snprintf(request.agent_name, sizeof(request.agent_name), "truck42");
        snprintf(request.interface_name, sizeof(request.interface_name), "can0");
        request.op = IFCONFIG_OP_QUERY;
        request.settings = fdSettings();

        encoded_size = ConfigureMessage_Encode(&request, buffer, sizeof(buffer));
        decoded_ok = ConfigureMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, CONFIGURE_BODY_SIZE);

        expect(encoded_size).toBe((size_t)(MESSAGE_HEADER_SIZE + CONFIGURE_BODY_SIZE));
        expect(decoded_ok).toBe(true);
        expect(std::string(decoded.agent_name)).toBe(std::string("truck42"));
        expect(std::string(decoded.interface_name)).toBe(std::string("can0"));
        expect(decoded.op).toBe(IFCONFIG_OP_QUERY);
        expect(decoded.settings.txqueuelen).toBe((uint32_t)1000);
    });

    it("round-trips a configure reply", []() {
        ConfigureReplyMessage reply;
        ConfigureReplyMessage decoded;
        uint8_t buffer[MESSAGE_HEADER_SIZE + CONFIGURE_REPLY_BODY_SIZE];
        size_t encoded_size;
        bool decoded_ok;

        memset(&reply, 0, sizeof(reply));
        reply.status = CONFIGURE_STATUS_DENIED;
        reply.settings = fdSettings();
        snprintf(reply.detail, sizeof(reply.detail), "no admin grant");

        encoded_size = ConfigureReplyMessage_Encode(&reply, buffer, sizeof(buffer));
        decoded_ok = ConfigureReplyMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, CONFIGURE_REPLY_BODY_SIZE);

        expect(encoded_size).toBe((size_t)(MESSAGE_HEADER_SIZE + CONFIGURE_REPLY_BODY_SIZE));
        expect(decoded_ok).toBe(true);
        expect(decoded.status).toBe(CONFIGURE_STATUS_DENIED);
        expect(decoded.settings.bitrate).toBe((uint32_t)500000);
        expect(std::string(decoded.detail)).toBe(std::string("no admin grant"));
    });

    it("refuses to encode an interface name with no terminator", []() {
        ConfigureMessage request;
        uint8_t buffer[MESSAGE_HEADER_SIZE + CONFIGURE_BODY_SIZE];
        size_t encoded_size;

        memset(&request, 0, sizeof(request));
        memset(request.interface_name, 'x', sizeof(request.interface_name));

        encoded_size = ConfigureMessage_Encode(&request, buffer, sizeof(buffer));

        expect(encoded_size).toBe((size_t)0);
    });

    it("rejects a short configure body", []() {
        ConfigureMessage decoded;
        uint8_t payload[CONFIGURE_BODY_SIZE - 1] = { 0 };

        expect(ConfigureMessage_Decode(&decoded, payload, sizeof(payload))).toBe(false);
    });
});

static InterfaceSettings fdSettings(void)
{
    InterfaceSettings settings;

    memset(&settings, 0, sizeof(settings));
    settings.fields = INTERFACE_FIELDS_SETTABLE;
    settings.bitrate = 500000;
    settings.data_bitrate = 2000000;
    settings.sample_point = 875;
    settings.data_sample_point = 750;
    settings.ctrlmode_mask = INTERFACE_CTRLMODE_FD | INTERFACE_CTRLMODE_LISTEN_ONLY;
    settings.ctrlmode_flags = INTERFACE_CTRLMODE_FD;
    settings.restart_ms = 100;
    settings.txqueuelen = 1000;
    settings.link_up = true;

    return settings;
}
