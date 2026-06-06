#include <cest>

extern "C" {
#include "hub/broker.h"
#include "broker_driver.h"
#include "hub_transport_port_mock.h"
#include "protocol/frame_message.h"
#include "protocol/hello_message.h"
#include "protocol/list_message.h"
#include "protocol/message_header.h"
#include "protocol/open_message.h"
}

#define AGENT_PEER 100
#define CLIENT_PEER 200

static Broker broker;
static HubTransportPortMock transport;
static HubTransportEvents events;
static uint8_t client_channel;
static const RegisterMessage truck_registration = { "truck42", 2, { "can0", "can1" } };

describe("broker", []() {
    describe("agent registration", []() {
        beforeEach([]() {
            HubTransportPortMock_Reset(&transport);
            Broker_Init(&broker, &transport.port);
            events = Broker_Events(&broker);
        });

        it("acknowledges a registration with channels", []() {
            HelloMessage hello = { PROTOCOL_VERSION, kPEER_ROLE_AGENT, 0 };
            RegisterAckMessage ack;
            MessageHeader header;
            uint8_t encoded[512];
            size_t encoded_size;

            events.on_peer_connected(events.context, AGENT_PEER);
            encoded_size = HelloMessage_Encode(&hello, encoded, sizeof(encoded));
            events.on_peer_control(events.context, AGENT_PEER, encoded, encoded_size, 0);
            encoded_size = RegisterMessage_Encode(&truck_registration, encoded, sizeof(encoded));

            events.on_peer_control(events.context, AGENT_PEER, encoded, encoded_size, 0);
            MessageHeader_Decode(&header, transport.control_log[0], transport.control_sizes[0]);
            RegisterAckMessage_Decode(&ack, transport.control_log[0] + MESSAGE_HEADER_SIZE, header.length);

            expect(transport.control_count).toBe(1);
            expect(transport.control_peers[0]).toBe((uint32_t)AGENT_PEER);
            expect(header.type).toBe(kMESSAGE_TYPE_REGISTER_ACK);
            expect(ack.status).toBe(REGISTER_STATUS_OK);
            expect(ack.channels[1]).toBe(1);
        });

        it("rejects and disconnects a colliding registration", []() {
            HelloMessage hello = { PROTOCOL_VERSION, kPEER_ROLE_AGENT, 0 };
            RegisterAckMessage ack;
            MessageHeader header;
            uint8_t encoded[512];
            uint8_t hello_encoded[64];
            size_t encoded_size;
            size_t hello_size;

            BrokerDriver_ConnectAgent(&events, &transport, AGENT_PEER, &truck_registration);
            events.on_peer_connected(events.context, 101);
            hello_size = HelloMessage_Encode(&hello, hello_encoded, sizeof(hello_encoded));
            events.on_peer_control(events.context, 101, hello_encoded, hello_size, 0);
            encoded_size = RegisterMessage_Encode(&truck_registration, encoded, sizeof(encoded));

            events.on_peer_control(events.context, 101, encoded, encoded_size, 0);
            MessageHeader_Decode(&header, transport.control_log[0], transport.control_sizes[0]);
            RegisterAckMessage_Decode(&ack, transport.control_log[0] + MESSAGE_HEADER_SIZE, header.length);

            expect(ack.status).toBe(1);
            expect(transport.close_count).toBe(1);
            expect(transport.last_closed_peer).toBe((uint32_t)101);
        });
    });

    describe("client control plane", []() {
        beforeEach([]() {
            HubTransportPortMock_Reset(&transport);
            Broker_Init(&broker, &transport.port);
            events = Broker_Events(&broker);
            BrokerDriver_ConnectAgent(&events, &transport, AGENT_PEER, &truck_registration);
            BrokerDriver_ConnectClient(&events, CLIENT_PEER);
        });

        it("answers list with the catalogue", []() {
            ListMessage list = { 0 };
            ListReplyMessage reply;
            MessageHeader header;
            uint8_t encoded[64];
            size_t encoded_size = ListMessage_Encode(&list, encoded, sizeof(encoded));

            events.on_peer_control(events.context, CLIENT_PEER, encoded, encoded_size, 0);
            MessageHeader_Decode(&header, transport.control_log[0], transport.control_sizes[0]);
            ListReplyMessage_Decode(&reply, transport.control_log[0] + MESSAGE_HEADER_SIZE, header.length);

            expect(header.type).toBe(kMESSAGE_TYPE_LIST_REPLY);
            expect(reply.count).toBe(2);
            expect((const char *)reply.entries[0].agent_name).toBe("truck42");
        });

        it("acknowledges an open with a channel", []() {
            uint32_t interface_id = BrokerDriver_InterfaceIdAt(&events, &transport, 0);
            OpenMessage open = { interface_id };
            OpenAckMessage ack;
            MessageHeader header;
            uint8_t encoded[64];
            size_t encoded_size = OpenMessage_Encode(&open, encoded, sizeof(encoded));

            events.on_peer_control(events.context, CLIENT_PEER, encoded, encoded_size, 0);
            MessageHeader_Decode(&header, transport.control_log[0], transport.control_sizes[0]);
            OpenAckMessage_Decode(&ack, transport.control_log[0] + MESSAGE_HEADER_SIZE, header.length);

            expect(header.type).toBe(kMESSAGE_TYPE_OPEN_ACK);
            expect(ack.status).toBe(OPEN_STATUS_OK);
            expect(ack.interface_id).toBe(interface_id);
        });

        it("rejects opening an unknown interface", []() {
            OpenMessage open = { 9999 };
            OpenAckMessage ack;
            MessageHeader header;
            uint8_t encoded[64];
            size_t encoded_size = OpenMessage_Encode(&open, encoded, sizeof(encoded));

            events.on_peer_control(events.context, CLIENT_PEER, encoded, encoded_size, 0);
            MessageHeader_Decode(&header, transport.control_log[0], transport.control_sizes[0]);
            OpenAckMessage_Decode(&ack, transport.control_log[0] + MESSAGE_HEADER_SIZE, header.length);

            expect(ack.status).toBe(1);
        });
    });

    describe("frame routing", []() {
        beforeEach([]() {
            uint32_t interface_id;

            HubTransportPortMock_Reset(&transport);
            Broker_Init(&broker, &transport.port);
            events = Broker_Events(&broker);
            BrokerDriver_ConnectAgent(&events, &transport, AGENT_PEER, &truck_registration);
            BrokerDriver_ConnectClient(&events, CLIENT_PEER);
            interface_id = BrokerDriver_InterfaceIdAt(&events, &transport, 1);
            client_channel = BrokerDriver_OpenInterface(&events, &transport, CLIENT_PEER, interface_id);
        });

        it("forwards an agent frame to the subscribed client", []() {
            FrameMessage frame = { 0x123, 1000, 1, 2, 0, { 0xAA, 0xBB } };
            FrameMessage routed;
            MessageHeader header;
            uint8_t encoded[128];
            size_t encoded_size = FrameMessage_Encode(&frame, encoded, sizeof(encoded));

            events.on_peer_frame(events.context, AGENT_PEER, encoded, encoded_size);
            MessageHeader_Decode(&header, transport.frame_log[0], transport.frame_sizes[0]);
            FrameMessage_Decode(&routed, transport.frame_log[0] + MESSAGE_HEADER_SIZE, header.length);

            expect(transport.frame_count).toBe(1);
            expect(transport.frame_peers[0]).toBe((uint32_t)CLIENT_PEER);
            expect(routed.can_id).toBe((uint32_t)0x123);
            expect(routed.channel).toBe(client_channel);
            expect(routed.payload).toEqualMemory(frame.payload, 2);
        });

        it("forwards a client frame to the owning agent with its channel", []() {
            FrameMessage frame = { 0x456, 2000, client_channel, 1, 0, { 0x55 } };
            FrameMessage routed;
            MessageHeader header;
            uint8_t encoded[128];
            size_t encoded_size = FrameMessage_Encode(&frame, encoded, sizeof(encoded));

            events.on_peer_frame(events.context, CLIENT_PEER, encoded, encoded_size);
            MessageHeader_Decode(&header, transport.frame_log[0], transport.frame_sizes[0]);
            FrameMessage_Decode(&routed, transport.frame_log[0] + MESSAGE_HEADER_SIZE, header.length);

            expect(transport.frame_count).toBe(1);
            expect(transport.frame_peers[0]).toBe((uint32_t)AGENT_PEER);
            expect(routed.channel).toBe(1);
        });

        it("drops agent frames on channels nobody opened", []() {
            FrameMessage frame = { 0x123, 1000, 0, 1, 0, { 0x55 } };
            uint8_t encoded[128];
            size_t encoded_size = FrameMessage_Encode(&frame, encoded, sizeof(encoded));

            events.on_peer_frame(events.context, AGENT_PEER, encoded, encoded_size);

            expect(transport.frame_count).toBe(0);
        });

        it("stops routing after the agent disconnects", []() {
            FrameMessage frame = { 0x456, 2000, client_channel, 1, 0, { 0x55 } };
            uint8_t encoded[128];
            size_t encoded_size = FrameMessage_Encode(&frame, encoded, sizeof(encoded));

            events.on_peer_disconnected(events.context, AGENT_PEER, 0);
            events.on_peer_frame(events.context, CLIENT_PEER, encoded, encoded_size);

            expect(transport.frame_count).toBe(0);
        });
    });

    describe("liveness", []() {
        beforeEach([]() {
            HubTransportPortMock_Reset(&transport);
            Broker_Init(&broker, &transport.port);
            events = Broker_Events(&broker);
            events.on_peer_connected(events.context, AGENT_PEER);
        });

        it("answers ping with pong", []() {
            MessageHeader ping = { kMESSAGE_TYPE_PING, 0, 0 };
            MessageHeader reply;
            uint8_t encoded[MESSAGE_HEADER_SIZE];

            MessageHeader_Encode(&ping, encoded, sizeof(encoded));

            events.on_peer_control(events.context, AGENT_PEER, encoded, sizeof(encoded), 0);
            MessageHeader_Decode(&reply, transport.control_log[0], transport.control_sizes[0]);

            expect(transport.control_count).toBe(1);
            expect(reply.type).toBe(kMESSAGE_TYPE_PING);
            expect(reply.flags).toBe(0x01);
        });
    });
});
