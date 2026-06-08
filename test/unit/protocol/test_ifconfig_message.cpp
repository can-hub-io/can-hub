#include <cest>

extern "C" {
#include "protocol/ifconfig_message.h"
#include "protocol/message_header.h"
}

describe("ifconfig_message", []() {
    it("round-trips the hub-to-agent request", []() {
        IfconfigMessage request = { "can0", IFCONFIG_OP_SET_BITRATE, 500000 };
        IfconfigMessage decoded;
        uint8_t buffer[MESSAGE_HEADER_SIZE + IFCONFIG_BODY_SIZE];
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = IfconfigMessage_Encode(&request, buffer, sizeof(buffer));
        decoded_ok = IfconfigMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, IFCONFIG_BODY_SIZE);

        expect(encoded_size).toBe((size_t)(MESSAGE_HEADER_SIZE + IFCONFIG_BODY_SIZE));
        expect(decoded_ok).toBe(true);
        expect(std::string(decoded.interface_name)).toBe(std::string("can0"));
        expect(decoded.op).toBe(IFCONFIG_OP_SET_BITRATE);
        expect(decoded.bitrate).toBe((uint32_t)500000);
    });

    it("round-trips the agent reply", []() {
        IfconfigReplyMessage reply = { "can1", IFCONFIG_STATUS_APPLY_FAILED };
        IfconfigReplyMessage decoded;
        uint8_t buffer[MESSAGE_HEADER_SIZE + IFCONFIG_REPLY_BODY_SIZE];
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = IfconfigReplyMessage_Encode(&reply, buffer, sizeof(buffer));
        decoded_ok = IfconfigReplyMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, IFCONFIG_REPLY_BODY_SIZE);

        expect(encoded_size).toBe((size_t)(MESSAGE_HEADER_SIZE + IFCONFIG_REPLY_BODY_SIZE));
        expect(decoded_ok).toBe(true);
        expect(std::string(decoded.interface_name)).toBe(std::string("can1"));
        expect(decoded.status).toBe(IFCONFIG_STATUS_APPLY_FAILED);
    });

    it("round-trips the admin request with the namespaced name", []() {
        AdminIfconfigMessage request;
        AdminIfconfigMessage decoded;
        uint8_t buffer[MESSAGE_HEADER_SIZE + ADMIN_IFCONFIG_BODY_SIZE];
        size_t encoded_size;
        bool decoded_ok;

        memset(&request, 0, sizeof(request));
        snprintf(request.agent_name, sizeof(request.agent_name), "truck42");
        snprintf(request.interface_name, sizeof(request.interface_name), "can0");
        request.op = IFCONFIG_OP_DOWN;
        request.bitrate = 0;

        encoded_size = AdminIfconfigMessage_Encode(&request, buffer, sizeof(buffer));
        decoded_ok = AdminIfconfigMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, ADMIN_IFCONFIG_BODY_SIZE);

        expect(encoded_size).toBe((size_t)(MESSAGE_HEADER_SIZE + ADMIN_IFCONFIG_BODY_SIZE));
        expect(decoded_ok).toBe(true);
        expect(std::string(decoded.agent_name)).toBe(std::string("truck42"));
        expect(std::string(decoded.interface_name)).toBe(std::string("can0"));
        expect(decoded.op).toBe(IFCONFIG_OP_DOWN);
    });

    it("round-trips the admin reply status", []() {
        AdminIfconfigReplyMessage reply = { ADMIN_IFCONFIG_STATUS_AGENT_UNREACHABLE };
        AdminIfconfigReplyMessage decoded;
        uint8_t buffer[MESSAGE_HEADER_SIZE + ADMIN_IFCONFIG_REPLY_BODY_SIZE];
        size_t encoded_size;
        bool decoded_ok;

        encoded_size = AdminIfconfigReplyMessage_Encode(&reply, buffer, sizeof(buffer));
        decoded_ok = AdminIfconfigReplyMessage_Decode(&decoded, buffer + MESSAGE_HEADER_SIZE, ADMIN_IFCONFIG_REPLY_BODY_SIZE);

        expect(encoded_size).toBe((size_t)(MESSAGE_HEADER_SIZE + ADMIN_IFCONFIG_REPLY_BODY_SIZE));
        expect(decoded_ok).toBe(true);
        expect(decoded.status).toBe(ADMIN_IFCONFIG_STATUS_AGENT_UNREACHABLE);
    });

    it("rejects truncated payloads", []() {
        IfconfigMessage request;
        AdminIfconfigMessage admin;
        uint8_t payload[ADMIN_IFCONFIG_BODY_SIZE] = { 0 };

        expect(IfconfigMessage_Decode(&request, payload, IFCONFIG_BODY_SIZE - 1)).toBe(false);
        expect(AdminIfconfigMessage_Decode(&admin, payload, ADMIN_IFCONFIG_BODY_SIZE - 1)).toBe(false);
    });
});
