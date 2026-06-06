#include <cest>

extern "C" {
#include "agent/agent.h"
#include "can_port_mock.h"
#include "protocol/hello_message.h"
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
            FrameMessage frame = { 0x123, 1000, 0, 2, 0, { 0xAA, 0xBB } };

            Agent_OnCanFrame(&agent, 0, &frame);

            expect(transport.frame_count).toBe(0);
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
            FrameMessage frame = { 0x123, 1000, 0, 2, 0, { 0xAA, 0xBB } };
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
            FrameMessage frame = { 0x456, 2000, 7, 3, 0, { 1, 2, 3 } };
            uint8_t encoded_frame[128];
            size_t encoded_frame_size = FrameMessage_Encode(&frame, encoded_frame, sizeof(encoded_frame));

            Agent_OnTransportFrame(&agent, encoded_frame, encoded_frame_size);

            expect(can.write_count).toBe(1);
            expect(can.last_interface_index).toBe(0);
            expect(can.last_frame.can_id).toBe((uint32_t)0x456);
        });

        it("ignores transport frames for unknown channels", []() {
            FrameMessage frame = { 0x456, 2000, 42, 3, 0, { 1, 2, 3 } };
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
    });

});