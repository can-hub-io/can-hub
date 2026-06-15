#include <cest>

extern "C" {
#include "agent/agent.h"
#include "can_port_mock.h"
#include "protocol/hello_message.h"
#include "protocol/ifconfig_message.h"
#include "protocol/interface_status_message.h"
#include "protocol/message_header.h"
#include "transport_port_mock.h"
}

static Agent agent;
static TransportPortMock transport;
static CanPortMock can;
static const RegisterMessage registration = { "truck42", 2, { "can0", "can1" } };
static const RegisterAckMessage ack_ok = { REGISTER_STATUS_OK, 2, { 7, 9 } };

describe("agent", []() {
    describe("disconnected", []() {
        beforeEach([]() {
            TransportPortMock_Reset(&transport);
            CanPortMock_Reset(&can);
            Agent_Init(&agent, &transport.port, &can.port, &registration);
        });

        it("connects on the first tick", []() {
            Agent_Tick(&agent, 0);

            expect(transport.connect_calls).toBe(1);
            expect(Agent_State(&agent)).toBe(kAGENT_STATE_CONNECTING);
        });

        it("doubles the backoff on repeated connect failures", []() {
            transport.connect_result = false;

            Agent_Tick(&agent, 0);
            Agent_Tick(&agent, 1000000);
            Agent_Tick(&agent, 1000000 + 1999999);
            Agent_Tick(&agent, 1000000 + 2000000);

            expect(transport.connect_calls).toBe(3);
            expect(Agent_State(&agent)).toBe(kAGENT_STATE_DISCONNECTED);
        });
    });

    describe("connecting", []() {
        beforeEach([]() {
            TransportPortMock_Reset(&transport);
            CanPortMock_Reset(&can);
            Agent_Init(&agent, &transport.port, &can.port, &registration);
            Agent_Tick(&agent, 0);
        });

        it("sends hello and register once connected", []() {
            MessageHeader first_header;
            MessageHeader second_header;
            RegisterMessage sent_registration;

            Agent_OnConnected(&agent);
            MessageHeader_Decode(&first_header, transport.control_log[0], transport.control_sizes[0]);
            MessageHeader_Decode(&second_header, transport.control_log[1], transport.control_sizes[1]);
            RegisterMessage_Decode(&sent_registration, transport.control_log[1] + MESSAGE_HEADER_SIZE, second_header.length);

            expect(transport.control_count).toBe(2);
            expect(first_header.type).toBe(kMESSAGE_TYPE_HELLO);
            expect(second_header.type).toBe(kMESSAGE_TYPE_REGISTER);
            expect((const char *)sent_registration.agent_name).toBe("truck42");
            expect(Agent_State(&agent)).toBe(kAGENT_STATE_REGISTERING);
        });
    });

    describe("registering", []() {
        beforeEach([]() {
            TransportPortMock_Reset(&transport);
            CanPortMock_Reset(&can);
            Agent_Init(&agent, &transport.port, &can.port, &registration);
            Agent_Tick(&agent, 0);
            Agent_OnConnected(&agent);
        });

        it("runs after a successful register ack", []() {
            uint8_t encoded[64];
            size_t encoded_size = RegisterAckMessage_Encode(&ack_ok, encoded, sizeof(encoded));

            Agent_OnControlMessage(&agent, encoded, encoded_size, 0);

            expect(Agent_State(&agent)).toBe(kAGENT_STATE_RUNNING);
        });

        it("disconnects when the register ack reports an error", []() {
            RegisterAckMessage ack_error = { 1, 0, { 0 } };
            uint8_t encoded[64];
            size_t encoded_size = RegisterAckMessage_Encode(&ack_error, encoded, sizeof(encoded));

            Agent_OnControlMessage(&agent, encoded, encoded_size, 0);

            expect(transport.disconnect_calls).toBe(1);
            expect(Agent_State(&agent)).toBe(kAGENT_STATE_DISCONNECTED);
        });

        it("drops can frames while not running", []() {
            FrameMessage frame = { 0x123, 1000, 0, 2, 0, 0, { 0xAA, 0xBB } };

            Agent_OnCanFrame(&agent, 0, &frame);

            expect(transport.frame_count).toBe(0);
        });

        it("stays registering while the ack timeout has not expired", []() {
            Agent_Tick(&agent, 1000000);
            Agent_Tick(&agent, 1000000 + AGENT_REGISTER_TIMEOUT_MS * 1000 - 1);

            expect(transport.disconnect_calls).toBe(0);
            expect(Agent_State(&agent)).toBe(kAGENT_STATE_REGISTERING);
        });

        it("disconnects when the register ack never arrives", []() {
            Agent_Tick(&agent, 1000000);
            Agent_Tick(&agent, 1000000 + AGENT_REGISTER_TIMEOUT_MS * 1000);

            expect(transport.disconnect_calls).toBe(1);
            expect(Agent_State(&agent)).toBe(kAGENT_STATE_DISCONNECTED);
        });

        it("does not time out after a successful ack", []() {
            uint8_t encoded[64];
            size_t encoded_size = RegisterAckMessage_Encode(&ack_ok, encoded, sizeof(encoded));

            Agent_Tick(&agent, 1000000);
            Agent_OnControlMessage(&agent, encoded, encoded_size, 2000000);
            Agent_Tick(&agent, 1000000 + AGENT_REGISTER_TIMEOUT_MS * 1000);

            expect(transport.disconnect_calls).toBe(0);
            expect(Agent_State(&agent)).toBe(kAGENT_STATE_RUNNING);
        });

        it("arms a fresh timeout on the next registration attempt", []() {
            uint8_t encoded[64];
            size_t encoded_size = RegisterAckMessage_Encode(&ack_ok, encoded, sizeof(encoded));
            uint64_t second_attempt_us = 1000000 + AGENT_REGISTER_TIMEOUT_MS * 1000 + 2000000;

            Agent_Tick(&agent, 1000000);
            Agent_Tick(&agent, 1000000 + AGENT_REGISTER_TIMEOUT_MS * 1000);
            Agent_Tick(&agent, second_attempt_us);
            Agent_OnConnected(&agent);
            Agent_Tick(&agent, second_attempt_us);
            Agent_OnControlMessage(&agent, encoded, encoded_size, second_attempt_us + 1000);

            expect(Agent_State(&agent)).toBe(kAGENT_STATE_RUNNING);
            expect(transport.disconnect_calls).toBe(1);
        });

        it("replies pong to a ping request", []() {
            MessageHeader ping = { kMESSAGE_TYPE_PING, 0, 0 };
            MessageHeader reply;
            uint8_t encoded[MESSAGE_HEADER_SIZE];
            int control_count_before = transport.control_count;

            MessageHeader_Encode(&ping, encoded, sizeof(encoded));

            Agent_OnControlMessage(&agent, encoded, sizeof(encoded), 0);
            MessageHeader_Decode(
                &reply,
                transport.control_log[control_count_before],
                transport.control_sizes[control_count_before]
            );

            expect(transport.control_count).toBe(control_count_before + 1);
            expect(reply.type).toBe(kMESSAGE_TYPE_PING);
            expect(reply.flags).toBe(0x01);
        });
    });

    describe("running", []() {
        beforeEach([]() {
            uint8_t encoded[64];
            size_t encoded_size;

            TransportPortMock_Reset(&transport);
            CanPortMock_Reset(&can);
            Agent_Init(&agent, &transport.port, &can.port, &registration);
            Agent_Tick(&agent, 0);
            Agent_OnConnected(&agent);
            encoded_size = RegisterAckMessage_Encode(&ack_ok, encoded, sizeof(encoded));
            Agent_OnControlMessage(&agent, encoded, encoded_size, 0);
        });

        it("forwards can frames with the assigned channel", []() {
            FrameMessage frame = { 0x123, 1000, 0, 2, 0, 0, { 0xAA, 0xBB } };
            FrameMessage sent_frame;
            MessageHeader sent_header;

            Agent_OnCanFrame(&agent, 1, &frame);
            MessageHeader_Decode(&sent_header, transport.last_frame, transport.last_frame_size);
            FrameMessage_Decode(&sent_frame, transport.last_frame + MESSAGE_HEADER_SIZE, sent_header.length);

            expect(transport.frame_count).toBe(1);
            expect(sent_frame.channel).toBe(9);
            expect(sent_frame.can_id).toBe((uint32_t)0x123);
            expect(sent_frame.payload).toEqualMemory(frame.payload, 2);
        });

        it("injects transport frames into the owning interface", []() {
            FrameMessage frame = { 0x456, 2000, 7, 3, 0, 0, { 1, 2, 3 } };
            uint8_t encoded_frame[128];
            size_t encoded_frame_size = FrameMessage_Encode(&frame, encoded_frame, sizeof(encoded_frame));

            Agent_OnTransportFrame(&agent, encoded_frame, encoded_frame_size);

            expect(can.write_count).toBe(1);
            expect(can.last_interface_index).toBe(0);
            expect(can.last_frame.can_id).toBe((uint32_t)0x456);
        });

        it("ignores transport frames for unknown channels", []() {
            FrameMessage frame = { 0x456, 2000, 42, 3, 0, 0, { 1, 2, 3 } };
            uint8_t encoded_frame[128];
            size_t encoded_frame_size = FrameMessage_Encode(&frame, encoded_frame, sizeof(encoded_frame));

            Agent_OnTransportFrame(&agent, encoded_frame, encoded_frame_size);

            expect(can.write_count).toBe(0);
        });

        it("reconnects only after the backoff delay", []() {
            Agent_OnDisconnected(&agent, 5000000);

            Agent_Tick(&agent, 5000000 + 999999);
            Agent_Tick(&agent, 5000000 + 1000000);

            expect(transport.connect_calls).toBe(2);
            expect(Agent_State(&agent)).toBe(kAGENT_STATE_CONNECTING);
        });

        it("returns the origin token on the bus echo of an injection", []() {
            FrameMessage injected = { 0x456, 2000, 7, 1, 0, 8 << FRAME_ROUTE_TOKEN_SHIFT, { 0x55 } };
            FrameMessage echo = { 0x456, 2100, 0, 1, 0, FRAME_ROUTE_FLAG_ECHO, { 0x55 } };
            FrameMessage sent_frame;
            MessageHeader sent_header;
            uint8_t encoded_frame[128];
            size_t encoded_frame_size = FrameMessage_Encode(&injected, encoded_frame, sizeof(encoded_frame));

            Agent_OnTransportFrame(&agent, encoded_frame, encoded_frame_size);
            Agent_OnCanFrame(&agent, 0, &echo);
            MessageHeader_Decode(&sent_header, transport.last_frame, transport.last_frame_size);
            FrameMessage_Decode(&sent_frame, transport.last_frame + MESSAGE_HEADER_SIZE, sent_header.length);

            expect(transport.frame_count).toBe(1);
            expect(sent_frame.route_flags).toBe(FRAME_ROUTE_FLAG_ECHO | (8 << FRAME_ROUTE_TOKEN_SHIFT));
        });

        it("forwards an unmatched echo without a token", []() {
            FrameMessage echo = { 0x456, 2100, 0, 1, 0, FRAME_ROUTE_FLAG_ECHO, { 0x55 } };
            FrameMessage sent_frame;
            MessageHeader sent_header;

            Agent_OnCanFrame(&agent, 0, &echo);
            MessageHeader_Decode(&sent_header, transport.last_frame, transport.last_frame_size);
            FrameMessage_Decode(&sent_frame, transport.last_frame + MESSAGE_HEADER_SIZE, sent_header.length);

            expect(sent_frame.route_flags).toBe(FRAME_ROUTE_FLAG_ECHO);
        });

        it("drops the pending token when the bus write fails", []() {
            FrameMessage injected = { 0x456, 2000, 7, 1, 0, 8 << FRAME_ROUTE_TOKEN_SHIFT, { 0x55 } };
            FrameMessage echo = { 0x456, 2100, 0, 1, 0, FRAME_ROUTE_FLAG_ECHO, { 0x55 } };
            FrameMessage sent_frame;
            MessageHeader sent_header;
            uint8_t encoded_frame[128];
            size_t encoded_frame_size = FrameMessage_Encode(&injected, encoded_frame, sizeof(encoded_frame));

            can.write_result = false;
            Agent_OnTransportFrame(&agent, encoded_frame, encoded_frame_size);
            Agent_OnCanFrame(&agent, 0, &echo);
            MessageHeader_Decode(&sent_header, transport.last_frame, transport.last_frame_size);
            FrameMessage_Decode(&sent_frame, transport.last_frame + MESSAGE_HEADER_SIZE, sent_header.length);

            expect(sent_frame.route_flags).toBe(FRAME_ROUTE_FLAG_ECHO);
        });

        it("strips stale route flags from genuine bus frames", []() {
            FrameMessage frame = { 0x123, 1000, 0, 1, 0, FRAME_ROUTE_TOKEN_MASK, { 0x55 } };
            FrameMessage sent_frame;
            MessageHeader sent_header;

            Agent_OnCanFrame(&agent, 0, &frame);
            MessageHeader_Decode(&sent_header, transport.last_frame, transport.last_frame_size);
            FrameMessage_Decode(&sent_frame, transport.last_frame + MESSAGE_HEADER_SIZE, sent_header.length);

            expect(sent_frame.route_flags).toBe(0);
        });

        it("applies an interface config and replies ok", []() {
            IfconfigMessage request = { "can1", IFCONFIG_OP_SET_BITRATE, 250000 };
            IfconfigReplyMessage reply;
            MessageHeader header;
            uint8_t encoded[64];
            size_t encoded_size = IfconfigMessage_Encode(&request, encoded, sizeof(encoded));
            int reply_index;

            Agent_OnControlMessage(&agent, encoded, encoded_size, 0);
            reply_index = transport.control_count - 1;
            MessageHeader_Decode(&header, transport.control_log[reply_index], transport.control_sizes[reply_index]);
            IfconfigReplyMessage_Decode(&reply, transport.control_log[reply_index] + MESSAGE_HEADER_SIZE, header.length);

            expect(can.configure_count).toBe(1);
            expect(can.last_configure_interface_index).toBe(1);
            expect(can.last_configure_op).toBe(IFCONFIG_OP_SET_BITRATE);
            expect(can.last_configure_bitrate).toBe((uint32_t)250000);
            expect(header.type).toBe(kMESSAGE_TYPE_IFCONFIG_REPLY);
            expect(reply.status).toBe(IFCONFIG_STATUS_OK);
        });

        it("replies unknown interface for a name it does not export", []() {
            IfconfigMessage request = { "can9", IFCONFIG_OP_UP, 0 };
            IfconfigReplyMessage reply;
            MessageHeader header;
            uint8_t encoded[64];
            size_t encoded_size = IfconfigMessage_Encode(&request, encoded, sizeof(encoded));
            int reply_index;

            Agent_OnControlMessage(&agent, encoded, encoded_size, 0);
            reply_index = transport.control_count - 1;
            MessageHeader_Decode(&header, transport.control_log[reply_index], transport.control_sizes[reply_index]);
            IfconfigReplyMessage_Decode(&reply, transport.control_log[reply_index] + MESSAGE_HEADER_SIZE, header.length);

            expect(can.configure_count).toBe(0);
            expect(reply.status).toBe(IFCONFIG_STATUS_UNKNOWN_INTERFACE);
        });

        it("replies apply failed when the adapter rejects the change", []() {
            IfconfigMessage request = { "can0", IFCONFIG_OP_DOWN, 0 };
            IfconfigReplyMessage reply;
            MessageHeader header;
            uint8_t encoded[64];
            size_t encoded_size = IfconfigMessage_Encode(&request, encoded, sizeof(encoded));
            int reply_index;

            can.configure_result = false;
            Agent_OnControlMessage(&agent, encoded, encoded_size, 0);
            reply_index = transport.control_count - 1;
            MessageHeader_Decode(&header, transport.control_log[reply_index], transport.control_sizes[reply_index]);
            IfconfigReplyMessage_Decode(&reply, transport.control_log[reply_index] + MESSAGE_HEADER_SIZE, header.length);

            expect(can.configure_count).toBe(1);
            expect(reply.status).toBe(IFCONFIG_STATUS_APPLY_FAILED);
        });

        it("reports tx-drop counters in a periodic interface status", []() {
            FrameMessage injected = { 0x456, 2000, 7, 1, 0, 0, { 0x55 } };
            InterfaceStatusMessage status;
            MessageHeader header;
            uint8_t encoded_frame[128];
            size_t encoded_frame_size = FrameMessage_Encode(&injected, encoded_frame, sizeof(encoded_frame));
            int status_index;

            can.write_result = false;
            Agent_OnTransportFrame(&agent, encoded_frame, encoded_frame_size);
            Agent_Tick(&agent, 1);

            status_index = transport.control_count - 1;
            MessageHeader_Decode(&header, transport.control_log[status_index], transport.control_sizes[status_index]);
            InterfaceStatusMessage_Decode(&status, transport.control_log[status_index] + MESSAGE_HEADER_SIZE, header.length);

            expect(header.type).toBe(kMESSAGE_TYPE_INTERFACE_STATUS);
            expect(status.interface_count).toBe(2);
            expect(status.entries[0].channel).toBe(7);
            expect(status.entries[0].tx_dropped).toBe((uint64_t)1);
            expect(status.entries[1].channel).toBe(9);
            expect(status.entries[1].tx_dropped).toBe((uint64_t)0);
        });

        it("does not re-emit the interface status until the period elapses", []() {
            int after_first;

            Agent_Tick(&agent, 1);
            after_first = transport.control_count;
            Agent_Tick(&agent, 1 + AGENT_STATUS_PERIOD_MS * 1000 - 1);

            expect(transport.control_count).toBe(after_first);
        });

        it("re-emits the interface status after the period elapses", []() {
            int after_first;

            Agent_Tick(&agent, 1);
            after_first = transport.control_count;
            Agent_Tick(&agent, 1 + AGENT_STATUS_PERIOD_MS * 1000);

            expect(transport.control_count).toBe(after_first + 1);
        });

        it("advertises the interface bitrate in the status", []() {
            InterfaceStatusMessage status;
            MessageHeader header;
            int status_index;

            can.bitrate_value = 500000;
            Agent_Tick(&agent, 1);

            status_index = transport.control_count - 1;
            MessageHeader_Decode(&header, transport.control_log[status_index], transport.control_sizes[status_index]);
            InterfaceStatusMessage_Decode(&status, transport.control_log[status_index] + MESSAGE_HEADER_SIZE, header.length);

            expect(status.entries[0].advertised_rate).toBe((uint32_t)500000);
            expect(status.entries[1].advertised_rate).toBe((uint32_t)500000);
        });
    });

});