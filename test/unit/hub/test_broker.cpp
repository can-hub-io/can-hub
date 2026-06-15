#include <cest>

extern "C" {
#include "hub/broker.h"
#include "broker_driver.h"
#include "hub_transport_port_mock.h"
#include "identity_store_mock.h"
#include "authorization_mock.h"
#include "protocol/admin_message.h"
#include "protocol/error_message.h"
#include "protocol/frame_message.h"
#include "protocol/hello_message.h"
#include "protocol/list_message.h"
#include "protocol/message_header.h"
#include "protocol/open_message.h"
#include "protocol/subscribe_message.h"
#include "protocol/ifconfig_message.h"
#include "protocol/interface_status_message.h"
}

#define AGENT_PEER 100
#define CLIENT_PEER 200
#define ADMIN_PEER 300
#define TRUCK_FINGERPRINT "aa11bb22cc33dd44ee55ff66aa77bb88cc99dd00ee11ff22aa33bb44cc55dd66"
#define OTHER_FINGERPRINT "0000000000000000000000000000000000000000000000000000000000000000"

static Broker broker;
static HubTransportPortMock transport;
static IdentityStoreMock identity_store;
static AuthorizationMock authorization;
static HubTransportEvents events;
static uint8_t client_channel;
static const RegisterMessage truck_registration = { "truck42", 2, { "can0", "can1" } };

static void connectPeer(uint32_t peer_id, const char *fingerprint, bool local)
{
    HubPeerConnectInfo info = { fingerprint, NULL, kPEER_TRANSPORT_TCP, local };

    events.on_peer_connected(events.context, peer_id, &info, 0);
}

static void connectClientWithFingerprint(uint32_t peer_id, const char *fingerprint)
{
    HelloMessage hello = { PROTOCOL_VERSION, kPEER_ROLE_CLIENT, 0, "" };
    uint8_t encoded[128];
    size_t encoded_size;

    connectPeer(peer_id, fingerprint, false);
    encoded_size = HelloMessage_Encode(&hello, encoded, sizeof(encoded));
    events.on_peer_control(events.context, peer_id, encoded, encoded_size, 0);
}

static uint8_t openInterface(uint32_t peer_id, uint32_t interface_id, uint8_t open_flags)
{
    OpenMessage open = { interface_id, open_flags };
    OpenAckMessage ack;
    MessageHeader header;
    uint8_t encoded[128];
    size_t encoded_size = OpenMessage_Encode(&open, encoded, sizeof(encoded));
    int reply_index;

    events.on_peer_control(events.context, peer_id, encoded, encoded_size, 0);
    reply_index = transport.control_count - 1;
    MessageHeader_Decode(&header, transport.control_log[reply_index], transport.control_sizes[reply_index]);
    OpenAckMessage_Decode(&ack, transport.control_log[reply_index] + MESSAGE_HEADER_SIZE, header.length);

    return ack.status == OPEN_STATUS_OK ? ack.channel : 0xFF;
}

static void registerAgentWithFingerprint(uint32_t peer_id, const char *fingerprint)
{
    HelloMessage hello = { PROTOCOL_VERSION, kPEER_ROLE_AGENT, 0, "" };
    uint8_t encoded[512];
    size_t encoded_size;

    connectPeer(peer_id, fingerprint, false);
    encoded_size = HelloMessage_Encode(&hello, encoded, sizeof(encoded));
    events.on_peer_control(events.context, peer_id, encoded, encoded_size, 0);
    encoded_size = RegisterMessage_Encode(&truck_registration, encoded, sizeof(encoded));
    events.on_peer_control(events.context, peer_id, encoded, encoded_size, 0);
}

static uint8_t registerAckStatus(void)
{
    RegisterAckMessage ack;
    MessageHeader header;

    MessageHeader_Decode(&header, transport.control_log[0], transport.control_sizes[0]);
    RegisterAckMessage_Decode(&ack, transport.control_log[0] + MESSAGE_HEADER_SIZE, header.length);

    return ack.status;
}

template <typename TMessage>
static uint8_t replyAt(int reply_index, TMessage *message, bool (*decode)(TMessage *, const uint8_t *, size_t))
{
    MessageHeader header;

    MessageHeader_Decode(&header, transport.control_log[reply_index], transport.control_sizes[reply_index]);
    decode(message, transport.control_log[reply_index] + MESSAGE_HEADER_SIZE, header.length);

    return header.type;
}

template <typename TMessage>
static uint8_t lastReply(TMessage *message, bool (*decode)(TMessage *, const uint8_t *, size_t))
{
    return replyAt(transport.control_count - 1, message, decode);
}

static void sendControlFrom(uint32_t peer_id, const uint8_t *encoded, size_t encoded_size)
{
    events.on_peer_control(events.context, peer_id, encoded, encoded_size, 0);
}

describe("broker", []() {
    describe("agent registration", []() {
        beforeEach([]() {
            HubTransportPortMock_Reset(&transport);
            Broker_Init(&broker, &transport.port, NULL, NULL, false);
            events = Broker_Events(&broker);
        });

        it("acknowledges a registration with channels", []() {
            HelloMessage hello = { PROTOCOL_VERSION, kPEER_ROLE_AGENT, 0, "" };
            RegisterAckMessage ack;
            MessageHeader header;
            uint8_t encoded[512];
            size_t encoded_size;

            connectPeer(AGENT_PEER, NULL, false);
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
            HelloMessage hello = { PROTOCOL_VERSION, kPEER_ROLE_AGENT, 0, "" };
            RegisterAckMessage ack;
            MessageHeader header;
            uint8_t encoded[512];
            uint8_t hello_encoded[128];
            size_t encoded_size;
            size_t hello_size;

            BrokerDriver_ConnectAgent(&events, &transport, AGENT_PEER, &truck_registration);
            connectPeer(101, NULL, false);
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

    describe("agent ghost displacement", []() {
        beforeEach([]() {
            HubTransportPortMock_Reset(&transport);
            Broker_Init(&broker, &transport.port, NULL, NULL, false);
            events = Broker_Events(&broker);
        });

        it("displaces a ghost agent when the same fingerprint reconnects", []() {
            registerAgentWithFingerprint(AGENT_PEER, TRUCK_FINGERPRINT);
            HubTransportPortMock_Reset(&transport);
            registerAgentWithFingerprint(AGENT_PEER + 1, TRUCK_FINGERPRINT);

            expect(registerAckStatus()).toBe(REGISTER_STATUS_OK);
            expect(transport.close_count).toBe(1);
            expect(transport.last_closed_peer).toBe((uint32_t)AGENT_PEER);
        });

        it("rejects a same-name reconnect with a different fingerprint", []() {
            registerAgentWithFingerprint(AGENT_PEER, TRUCK_FINGERPRINT);
            HubTransportPortMock_Reset(&transport);
            registerAgentWithFingerprint(AGENT_PEER + 1, OTHER_FINGERPRINT);

            expect(registerAckStatus()).toBe(1);
            expect(transport.close_count).toBe(1);
            expect(transport.last_closed_peer).toBe((uint32_t)(AGENT_PEER + 1));
        });

        it("does not displace when neither peer carries a fingerprint", []() {
            registerAgentWithFingerprint(AGENT_PEER, NULL);
            HubTransportPortMock_Reset(&transport);
            registerAgentWithFingerprint(AGENT_PEER + 1, NULL);

            expect(registerAckStatus()).toBe(1);
            expect(transport.close_count).toBe(1);
            expect(transport.last_closed_peer).toBe((uint32_t)(AGENT_PEER + 1));
        });
    });

    describe("agent identity pinning", []() {
        beforeEach([]() {
            HubTransportPortMock_Reset(&transport);
            IdentityStoreMock_Reset(&identity_store);
            Broker_Init(&broker, &transport.port, &identity_store.port, NULL, false);
            events = Broker_Events(&broker);
        });

        it("pins the fingerprint of a first-seen agent name", []() {
            registerAgentWithFingerprint(AGENT_PEER, TRUCK_FINGERPRINT);

            expect(registerAckStatus()).toBe(REGISTER_STATUS_OK);
            expect(identity_store.pin_calls).toBe(1);
            expect((const char *)identity_store.names[0]).toBe("truck42");
            expect((const char *)identity_store.fingerprints[0]).toBe(TRUCK_FINGERPRINT);
        });

        it("accepts a registration matching the pinned fingerprint", []() {
            IdentityStoreMock_Preload(&identity_store, "truck42", TRUCK_FINGERPRINT);

            registerAgentWithFingerprint(AGENT_PEER, TRUCK_FINGERPRINT);

            expect(registerAckStatus()).toBe(REGISTER_STATUS_OK);
            expect(identity_store.pin_calls).toBe(0);
        });

        it("rejects and disconnects a registration with a changed fingerprint", []() {
            IdentityStoreMock_Preload(&identity_store, "truck42", TRUCK_FINGERPRINT);

            registerAgentWithFingerprint(AGENT_PEER, OTHER_FINGERPRINT);

            expect(registerAckStatus()).toBe(REGISTER_STATUS_IDENTITY_MISMATCH);
            expect(transport.close_count).toBe(1);
            expect(transport.last_closed_peer).toBe((uint32_t)AGENT_PEER);
        });

        it("skips pinning for peers without a fingerprint", []() {
            registerAgentWithFingerprint(AGENT_PEER, NULL);

            expect(registerAckStatus()).toBe(REGISTER_STATUS_OK);
            expect(identity_store.lookup_calls).toBe(0);
            expect(identity_store.pin_calls).toBe(0);
        });
    });

    describe("locked agents", []() {
        beforeEach([]() {
            HubTransportPortMock_Reset(&transport);
            IdentityStoreMock_Reset(&identity_store);
            Broker_Init(&broker, &transport.port, &identity_store.port, NULL, true);
            events = Broker_Events(&broker);
        });

        it("rejects an unknown fingerprint instead of auto-pinning", []() {
            ErrorMessage error;
            uint8_t error_type;

            registerAgentWithFingerprint(AGENT_PEER, TRUCK_FINGERPRINT);
            error_type = lastReply(&error, ErrorMessage_Decode);

            expect(registerAckStatus()).toBe(REGISTER_STATUS_UNKNOWN_AGENT);
            expect(identity_store.pin_calls).toBe(0);
            expect(error_type).toBe(kMESSAGE_TYPE_ERROR);
            expect(transport.close_count).toBe(1);
            expect(transport.last_closed_peer).toBe((uint32_t)AGENT_PEER);
        });

        it("accepts a pre-authorized fingerprint", []() {
            IdentityStoreMock_Preload(&identity_store, "truck42", TRUCK_FINGERPRINT);

            registerAgentWithFingerprint(AGENT_PEER, TRUCK_FINGERPRINT);

            expect(registerAckStatus()).toBe(REGISTER_STATUS_OK);
        });

        it("releases the peer slot of a rejected agent (no ghost)", []() {
            AdminPeersMessage request = { 0 };
            AdminPeersReplyMessage reply;
            uint8_t encoded[128];
            size_t encoded_size = AdminPeersMessage_Encode(&request, encoded, sizeof(encoded));

            registerAgentWithFingerprint(AGENT_PEER, TRUCK_FINGERPRINT);
            registerAgentWithFingerprint(AGENT_PEER + 1, OTHER_FINGERPRINT);
            HubTransportPortMock_Reset(&transport);
            BrokerDriver_ConnectAdmin(&events, ADMIN_PEER);
            sendControlFrom(ADMIN_PEER, encoded, encoded_size);
            lastReply(&reply, AdminPeersReplyMessage_Decode);

            expect(reply.count).toBe(1);
            expect(reply.entries[0].peer_id).toBe((uint32_t)ADMIN_PEER);
        });

        it("still accepts plaintext peers that carry no fingerprint", []() {
            registerAgentWithFingerprint(AGENT_PEER, NULL);

            expect(registerAckStatus()).toBe(REGISTER_STATUS_OK);
        });
    });

    describe("admin pin add", []() {
        beforeEach([]() {
            HubTransportPortMock_Reset(&transport);
            IdentityStoreMock_Reset(&identity_store);
            Broker_Init(&broker, &transport.port, &identity_store.port, NULL, true);
            events = Broker_Events(&broker);
            BrokerDriver_ConnectAdmin(&events, ADMIN_PEER);
        });

        it("pins an agent fingerprint from the admin plane", []() {
            AdminPinAddMessage request;
            AdminPinAddReplyMessage reply;
            uint8_t reply_type;
            uint8_t encoded[256];
            size_t encoded_size;

            memset(&request, 0, sizeof(request));
            snprintf(request.agent_name, sizeof(request.agent_name), "truck42");
            snprintf(request.fingerprint_hex, sizeof(request.fingerprint_hex), "%s", TRUCK_FINGERPRINT);
            encoded_size = AdminPinAddMessage_Encode(&request, encoded, sizeof(encoded));

            sendControlFrom(ADMIN_PEER, encoded, encoded_size);
            reply_type = lastReply(&reply, AdminPinAddReplyMessage_Decode);

            expect(reply_type).toBe(kMESSAGE_TYPE_ADMIN_PIN_ADD_REPLY);
            expect(reply.status).toBe(ADMIN_STATUS_OK);
            expect((const char *)identity_store.names[0]).toBe("truck42");
            expect((const char *)identity_store.fingerprints[0]).toBe(TRUCK_FINGERPRINT);
        });
    });

    describe("client control plane", []() {
        beforeEach([]() {
            HubTransportPortMock_Reset(&transport);
            Broker_Init(&broker, &transport.port, NULL, NULL, false);
            events = Broker_Events(&broker);
            BrokerDriver_ConnectAgent(&events, &transport, AGENT_PEER, &truck_registration);
            BrokerDriver_ConnectClient(&events, CLIENT_PEER);
        });

        it("answers list with the catalogue", []() {
            ListMessage list = { 0 };
            ListReplyMessage reply;
            MessageHeader header;
            uint8_t encoded[128];
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
            OpenMessage open = { interface_id, 0 };
            OpenAckMessage ack;
            MessageHeader header;
            uint8_t encoded[128];
            size_t encoded_size = OpenMessage_Encode(&open, encoded, sizeof(encoded));

            events.on_peer_control(events.context, CLIENT_PEER, encoded, encoded_size, 0);
            MessageHeader_Decode(&header, transport.control_log[0], transport.control_sizes[0]);
            OpenAckMessage_Decode(&ack, transport.control_log[0] + MESSAGE_HEADER_SIZE, header.length);

            expect(header.type).toBe(kMESSAGE_TYPE_OPEN_ACK);
            expect(ack.status).toBe(OPEN_STATUS_OK);
            expect(ack.interface_id).toBe(interface_id);
        });

        it("rejects opening an unknown interface", []() {
            OpenMessage open = { 9999, 0 };
            OpenAckMessage ack;
            MessageHeader header;
            uint8_t encoded[128];
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
            Broker_Init(&broker, &transport.port, NULL, NULL, false);
            events = Broker_Events(&broker);
            BrokerDriver_ConnectAgent(&events, &transport, AGENT_PEER, &truck_registration);
            BrokerDriver_ConnectClient(&events, CLIENT_PEER);
            interface_id = BrokerDriver_InterfaceIdAt(&events, &transport, 1);
            client_channel = BrokerDriver_OpenInterface(&events, &transport, CLIENT_PEER, interface_id, 0);
        });

        it("forwards an agent frame to the subscribed client", []() {
            FrameMessage frame = { 0x123, 1000, 1, 2, 0, 0, { 0xAA, 0xBB } };
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
            FrameMessage frame = { 0x456, 2000, client_channel, 1, 0, 0, { 0x55 } };
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
            FrameMessage frame = { 0x123, 1000, 0, 1, 0, 0, { 0x55 } };
            uint8_t encoded[128];
            size_t encoded_size = FrameMessage_Encode(&frame, encoded, sizeof(encoded));

            events.on_peer_frame(events.context, AGENT_PEER, encoded, encoded_size);

            expect(transport.frame_count).toBe(0);
        });

        it("stops routing after the agent disconnects", []() {
            FrameMessage frame = { 0x456, 2000, client_channel, 1, 0, 0, { 0x55 } };
            uint8_t encoded[128];
            size_t encoded_size = FrameMessage_Encode(&frame, encoded, sizeof(encoded));

            events.on_peer_disconnected(events.context, AGENT_PEER, 0);
            events.on_peer_frame(events.context, CLIENT_PEER, encoded, encoded_size);

            expect(transport.frame_count).toBe(0);
        });

        it("paces writes to a rate-limited agent interface, parking the excess", []() {
            InterfaceStatusMessage status;
            FrameMessage frame = { 0x456, 2000, client_channel, 64, FRAME_FLAG_FD, 0, { 0x55 } };
            uint8_t encoded[512];
            size_t encoded_size;
            uint8_t i;

            memset(&status, 0, sizeof(status));
            status.interface_count = 1;
            status.entries[0].channel = 1;
            status.entries[0].advertised_rate = 1000;
            encoded_size = InterfaceStatusMessage_Encode(&status, encoded, sizeof(encoded));
            sendControlFrom(AGENT_PEER, encoded, encoded_size);

            encoded_size = FrameMessage_Encode(&frame, encoded, sizeof(encoded));
            for(i=0; i<14; i++) {
                events.on_peer_frame(events.context, CLIENT_PEER, encoded, encoded_size);
            }

            expect(transport.frame_count > 0).toBe(true);
            expect(transport.frame_count < 14).toBe(true);
        });

        it("drains the parked paced frames as credit refills over time", []() {
            InterfaceStatusMessage status;
            FrameMessage frame = { 0x456, 2000, client_channel, 64, FRAME_FLAG_FD, 0, { 0x55 } };
            uint8_t encoded[512];
            size_t encoded_size;
            int parked_at;
            uint8_t i;

            memset(&status, 0, sizeof(status));
            status.interface_count = 1;
            status.entries[0].channel = 1;
            status.entries[0].advertised_rate = 1000;
            encoded_size = InterfaceStatusMessage_Encode(&status, encoded, sizeof(encoded));
            sendControlFrom(AGENT_PEER, encoded, encoded_size);

            encoded_size = FrameMessage_Encode(&frame, encoded, sizeof(encoded));
            for(i=0; i<14; i++) {
                events.on_peer_frame(events.context, CLIENT_PEER, encoded, encoded_size);
            }
            parked_at = transport.frame_count;

            Broker_Tick(&broker, 20000000);

            expect(transport.frame_count > parked_at).toBe(true);
        });

        it("keeps the full poll timeout when no paced frames wait", []() {
            expect(Broker_NextTimeoutMs(&broker, 100)).toBe(100);
        });

        it("shortens the poll timeout while paced frames await credit", []() {
            InterfaceStatusMessage status;
            FrameMessage frame = { 0x456, 2000, client_channel, 64, FRAME_FLAG_FD, 0, { 0x55 } };
            uint8_t encoded[512];
            size_t encoded_size;
            uint8_t i;

            memset(&status, 0, sizeof(status));
            status.interface_count = 1;
            status.entries[0].channel = 1;
            status.entries[0].advertised_rate = 100000;
            encoded_size = InterfaceStatusMessage_Encode(&status, encoded, sizeof(encoded));
            sendControlFrom(AGENT_PEER, encoded, encoded_size);

            encoded_size = FrameMessage_Encode(&frame, encoded, sizeof(encoded));
            for(i=0; i<14; i++) {
                events.on_peer_frame(events.context, CLIENT_PEER, encoded, encoded_size);
            }

            expect(Broker_NextTimeoutMs(&broker, 100) < 100).toBe(true);
        });

        it("delivers only frames matching the channel subscribe filter", []() {
            SubscribeMessage subscribe = { client_channel, 1, { { 0x100, 0x700 } } };
            FrameMessage matching = { 0x123, 1000, 1, 1, 0, 0, { 0x11 } };
            FrameMessage other = { 0x223, 1000, 1, 1, 0, 0, { 0x22 } };
            FrameMessage routed;
            MessageHeader header;
            uint8_t encoded[128];
            size_t encoded_size;

            encoded_size = SubscribeMessage_Encode(&subscribe, encoded, sizeof(encoded));
            sendControlFrom(CLIENT_PEER, encoded, encoded_size);

            encoded_size = FrameMessage_Encode(&other, encoded, sizeof(encoded));
            events.on_peer_frame(events.context, AGENT_PEER, encoded, encoded_size);
            encoded_size = FrameMessage_Encode(&matching, encoded, sizeof(encoded));
            events.on_peer_frame(events.context, AGENT_PEER, encoded, encoded_size);

            MessageHeader_Decode(&header, transport.frame_log[0], transport.frame_sizes[0]);
            FrameMessage_Decode(&routed, transport.frame_log[0] + MESSAGE_HEADER_SIZE, header.length);

            expect(transport.frame_count).toBe(1);
            expect(routed.can_id).toBe((uint32_t)0x123);
        });

        it("resumes passing every frame after an empty subscribe", []() {
            SubscribeMessage filtered = { client_channel, 1, { { 0x100, 0x700 } } };
            SubscribeMessage cleared = { client_channel, 0, {} };
            FrameMessage other = { 0x223, 1000, 1, 1, 0, 0, { 0x22 } };
            uint8_t encoded[128];
            size_t encoded_size;

            encoded_size = SubscribeMessage_Encode(&filtered, encoded, sizeof(encoded));
            sendControlFrom(CLIENT_PEER, encoded, encoded_size);
            encoded_size = SubscribeMessage_Encode(&cleared, encoded, sizeof(encoded));
            sendControlFrom(CLIENT_PEER, encoded, encoded_size);

            encoded_size = FrameMessage_Encode(&other, encoded, sizeof(encoded));
            events.on_peer_frame(events.context, AGENT_PEER, encoded, encoded_size);

            expect(transport.frame_count).toBe(1);
        });
    });

    describe("liveness", []() {
        beforeEach([]() {
            HubTransportPortMock_Reset(&transport);
            Broker_Init(&broker, &transport.port, NULL, NULL, false);
            events = Broker_Events(&broker);
            connectPeer(AGENT_PEER, NULL, false);
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

    describe("admin plane", []() {
        beforeEach([]() {
            HubTransportPortMock_Reset(&transport);
            IdentityStoreMock_Reset(&identity_store);
            Broker_Init(&broker, &transport.port, &identity_store.port, NULL, false);
            events = Broker_Events(&broker);
            BrokerDriver_ConnectAgent(&events, &transport, AGENT_PEER, &truck_registration);
            BrokerDriver_ConnectClient(&events, CLIENT_PEER);
            BrokerDriver_ConnectAdmin(&events, ADMIN_PEER);
        });

        it("closes a non-local peer claiming the admin role telling it why", []() {
            HelloMessage hello = { PROTOCOL_VERSION, kPEER_ROLE_ADMIN, 0, "" };
            ErrorMessage error;
            uint8_t error_type;
            uint8_t encoded[128];
            size_t encoded_size = HelloMessage_Encode(&hello, encoded, sizeof(encoded));

            connectPeer(999, NULL, false);
            sendControlFrom(999, encoded, encoded_size);
            error_type = lastReply(&error, ErrorMessage_Decode);

            expect(error_type).toBe(kMESSAGE_TYPE_ERROR);
            expect(error.code).toBe(kERROR_CODE_ROLE_REJECTED);
            expect(transport.close_count).toBe(1);
            expect(transport.last_closed_peer).toBe((uint32_t)999);
        });

        it("answers status with peer and interface counters", []() {
            AdminStatusReplyMessage reply;
            uint8_t reply_type;
            uint8_t encoded[128];
            size_t encoded_size = AdminStatusMessage_Encode(encoded, sizeof(encoded));

            sendControlFrom(ADMIN_PEER, encoded, encoded_size);
            reply_type = lastReply(&reply, AdminStatusReplyMessage_Decode);

            expect(reply_type).toBe(kMESSAGE_TYPE_ADMIN_STATUS_REPLY);
            expect(reply.peer_count).toBe(3);
            expect(reply.agent_count).toBe(1);
            expect(reply.client_count).toBe(1);
            expect(reply.interface_count).toBe(2);
        });

        it("ignores admin requests from non-admin peers", []() {
            uint8_t encoded[128];
            size_t encoded_size = AdminStatusMessage_Encode(encoded, sizeof(encoded));

            sendControlFrom(CLIENT_PEER, encoded, encoded_size);

            expect(transport.control_count).toBe(0);
        });

        it("records agent tx-drop counters and surfaces them in interfaces", []() {
            InterfaceStatusMessage status;
            AdminInterfacesMessage request = { 0 };
            AdminInterfacesReplyMessage reply;
            uint8_t reply_type;
            uint8_t encoded[512];
            size_t encoded_size;

            memset(&status, 0, sizeof(status));
            status.interface_count = 2;
            status.entries[0].channel = 0;
            status.entries[1].channel = 1;
            status.entries[1].tx_dropped = 17;
            encoded_size = InterfaceStatusMessage_Encode(&status, encoded, sizeof(encoded));
            sendControlFrom(AGENT_PEER, encoded, encoded_size);

            encoded_size = AdminInterfacesMessage_Encode(&request, encoded, sizeof(encoded));
            sendControlFrom(ADMIN_PEER, encoded, encoded_size);
            reply_type = lastReply(&reply, AdminInterfacesReplyMessage_Decode);

            expect(reply_type).toBe(kMESSAGE_TYPE_ADMIN_INTERFACES_REPLY);
            expect(reply.count).toBe(2);
            expect((const char *)reply.entries[1].interface_name).toBe("can1");
            expect(reply.entries[1].tx_dropped).toBe((uint64_t)17);
            expect(reply.entries[0].tx_dropped).toBe((uint64_t)0);
        });

        it("ignores an interface status from a non-agent peer", []() {
            InterfaceStatusMessage status;
            AdminInterfacesMessage request = { 0 };
            AdminInterfacesReplyMessage reply;
            uint8_t encoded[512];
            size_t encoded_size;

            memset(&status, 0, sizeof(status));
            status.interface_count = 2;
            status.entries[1].channel = 1;
            status.entries[1].tx_dropped = 99;
            encoded_size = InterfaceStatusMessage_Encode(&status, encoded, sizeof(encoded));
            sendControlFrom(CLIENT_PEER, encoded, encoded_size);

            encoded_size = AdminInterfacesMessage_Encode(&request, encoded, sizeof(encoded));
            sendControlFrom(ADMIN_PEER, encoded, encoded_size);
            lastReply(&reply, AdminInterfacesReplyMessage_Decode);

            expect(reply.entries[1].tx_dropped).toBe((uint64_t)0);
        });

        it("lists the live peers with role and agent name", []() {
            AdminPeersMessage request = { 0 };
            AdminPeersReplyMessage reply;
            uint8_t reply_type;
            uint8_t encoded[128];
            size_t encoded_size = AdminPeersMessage_Encode(&request, encoded, sizeof(encoded));

            sendControlFrom(ADMIN_PEER, encoded, encoded_size);
            reply_type = lastReply(&reply, AdminPeersReplyMessage_Decode);

            expect(reply_type).toBe(kMESSAGE_TYPE_ADMIN_PEERS_REPLY);
            expect(reply.count).toBe(3);
            expect(reply.entries[0].peer_id).toBe((uint32_t)AGENT_PEER);
            expect(reply.entries[0].role).toBe(kPEER_ROLE_AGENT);
            expect((const char *)reply.entries[0].agent_name).toBe("truck42");
            expect(reply.entries[1].role).toBe(kPEER_ROLE_CLIENT);
            expect(reply.entries[2].role).toBe(kPEER_ROLE_ADMIN);
        });

        it("surfaces a client name carried in its HELLO", []() {
            AdminPeersMessage request = { 0 };
            AdminPeersReplyMessage reply;
            HelloMessage hello = { PROTOCOL_VERSION, kPEER_ROLE_CLIENT, 0, "dashboard" };
            uint8_t encoded[128];
            size_t encoded_size;
            const char *named = "";
            uint8_t i;

            connectPeer(777, NULL, false);
            encoded_size = HelloMessage_Encode(&hello, encoded, sizeof(encoded));
            sendControlFrom(777, encoded, encoded_size);

            encoded_size = AdminPeersMessage_Encode(&request, encoded, sizeof(encoded));
            sendControlFrom(ADMIN_PEER, encoded, encoded_size);
            lastReply(&reply, AdminPeersReplyMessage_Decode);

            for(i=0; i<reply.count; i++) {
                if (reply.entries[i].peer_id == 777) {
                    named = reply.entries[i].agent_name;
                }
            }
            expect((const char *)named).toBe("dashboard");
        });

        it("kicks a registered agent by name", []() {
            AdminKickMessage kick = { "truck42" };
            AdminKickReplyMessage reply;
            ListMessage list = { 0 };
            ListReplyMessage catalogue;
            uint8_t reply_type;
            uint8_t encoded[256];
            size_t encoded_size = AdminKickMessage_Encode(&kick, encoded, sizeof(encoded));

            sendControlFrom(ADMIN_PEER, encoded, encoded_size);
            reply_type = replyAt(transport.control_count - 2, &reply, AdminKickReplyMessage_Decode);

            expect(reply_type).toBe(kMESSAGE_TYPE_ADMIN_KICK_REPLY);
            expect(reply.status).toBe(ADMIN_STATUS_OK);
            expect(transport.close_count).toBe(1);
            expect(transport.last_closed_peer).toBe((uint32_t)AGENT_PEER);

            encoded_size = ListMessage_Encode(&list, encoded, sizeof(encoded));
            sendControlFrom(ADMIN_PEER, encoded, encoded_size);
            lastReply(&catalogue, ListReplyMessage_Decode);
            expect(catalogue.count).toBe(0);
        });

        it("rejects kicking an unknown agent", []() {
            AdminKickMessage kick = { "ghost" };
            AdminKickReplyMessage reply;
            uint8_t encoded[256];
            size_t encoded_size = AdminKickMessage_Encode(&kick, encoded, sizeof(encoded));

            sendControlFrom(ADMIN_PEER, encoded, encoded_size);
            lastReply(&reply, AdminKickReplyMessage_Decode);

            expect(reply.status).toBe(ADMIN_STATUS_UNKNOWN_AGENT);
            expect(transport.close_count).toBe(0);
        });

        it("lists the pinned identities", []() {
            AdminPinsMessage request = { 0 };
            AdminPinsReplyMessage reply;
            uint8_t reply_type;
            uint8_t encoded[128];
            size_t encoded_size = AdminPinsMessage_Encode(&request, encoded, sizeof(encoded));

            IdentityStoreMock_Preload(&identity_store, "truck42", TRUCK_FINGERPRINT);

            sendControlFrom(ADMIN_PEER, encoded, encoded_size);
            reply_type = lastReply(&reply, AdminPinsReplyMessage_Decode);

            expect(reply_type).toBe(kMESSAGE_TYPE_ADMIN_PINS_REPLY);
            expect(reply.count).toBe(1);
            expect((const char *)reply.entries[0].agent_name).toBe("truck42");
            expect((const char *)reply.entries[0].fingerprint_hex).toBe(TRUCK_FINGERPRINT);
        });

        it("forgets a pinned identity", []() {
            AdminForgetMessage forget = { "truck42" };
            AdminForgetReplyMessage reply;
            uint8_t reply_type;
            uint8_t encoded[256];
            size_t encoded_size = AdminForgetMessage_Encode(&forget, encoded, sizeof(encoded));

            IdentityStoreMock_Preload(&identity_store, "truck42", TRUCK_FINGERPRINT);

            sendControlFrom(ADMIN_PEER, encoded, encoded_size);
            reply_type = lastReply(&reply, AdminForgetReplyMessage_Decode);

            expect(reply_type).toBe(kMESSAGE_TYPE_ADMIN_FORGET_REPLY);
            expect(reply.status).toBe(ADMIN_STATUS_OK);
            expect(identity_store.forget_calls).toBe(1);
            expect(identity_store.entry_count).toBe(0);
        });

        it("kicks any peer by id telling it why", []() {
            AdminKickPeerMessage kick = { CLIENT_PEER };
            AdminKickPeerReplyMessage reply;
            ErrorMessage error;
            uint8_t reply_type;
            uint8_t error_type;
            uint8_t encoded[128];
            size_t encoded_size = AdminKickPeerMessage_Encode(&kick, encoded, sizeof(encoded));

            sendControlFrom(ADMIN_PEER, encoded, encoded_size);
            reply_type = replyAt(transport.control_count - 2, &reply, AdminKickPeerReplyMessage_Decode);
            error_type = lastReply(&error, ErrorMessage_Decode);

            expect(reply_type).toBe(kMESSAGE_TYPE_ADMIN_KICK_PEER_REPLY);
            expect(reply.status).toBe(ADMIN_STATUS_OK);
            expect(error_type).toBe(kMESSAGE_TYPE_ERROR);
            expect(error.code).toBe(kERROR_CODE_KICKED);
            expect(transport.control_peers[transport.control_count - 1]).toBe((uint32_t)CLIENT_PEER);
            expect(transport.close_count).toBe(1);
            expect(transport.last_closed_peer).toBe((uint32_t)CLIENT_PEER);
        });

        it("rejects kicking an unknown peer id", []() {
            AdminKickPeerMessage kick = { 0xDEAD };
            AdminKickPeerReplyMessage reply;
            uint8_t encoded[128];
            size_t encoded_size = AdminKickPeerMessage_Encode(&kick, encoded, sizeof(encoded));

            sendControlFrom(ADMIN_PEER, encoded, encoded_size);
            lastReply(&reply, AdminKickPeerReplyMessage_Decode);

            expect(reply.status).toBe(ADMIN_STATUS_UNKNOWN_PEER);
            expect(transport.close_count).toBe(0);
        });

        it("lists the agents with their interface count", []() {
            AdminAgentsMessage request = { 0, "" };
            AdminAgentsReplyMessage reply;
            uint8_t reply_type;
            uint8_t encoded[256];
            size_t encoded_size = AdminAgentsMessage_Encode(&request, encoded, sizeof(encoded));

            sendControlFrom(ADMIN_PEER, encoded, encoded_size);
            reply_type = lastReply(&reply, AdminAgentsReplyMessage_Decode);

            expect(reply_type).toBe(kMESSAGE_TYPE_ADMIN_AGENTS_REPLY);
            expect(reply.count).toBe(1);
            expect(reply.entries[0].peer_id).toBe((uint32_t)AGENT_PEER);
            expect(reply.entries[0].interface_count).toBe(2);
            expect((const char *)reply.entries[0].agent_name).toBe("truck42");
        });

        it("lists the open client channels", []() {
            AdminClientsMessage request = { 0, "truck42" };
            AdminClientsReplyMessage reply;
            uint8_t reply_type;
            uint8_t encoded[256];
            size_t encoded_size;
            uint32_t interface_id = BrokerDriver_InterfaceIdAt(&events, &transport, 0);
            uint8_t channel = BrokerDriver_OpenInterface(&events, &transport, CLIENT_PEER, interface_id, 0);

            encoded_size = AdminClientsMessage_Encode(&request, encoded, sizeof(encoded));
            sendControlFrom(ADMIN_PEER, encoded, encoded_size);
            reply_type = lastReply(&reply, AdminClientsReplyMessage_Decode);

            expect(reply_type).toBe(kMESSAGE_TYPE_ADMIN_CLIENTS_REPLY);
            expect(reply.count).toBe(1);
            expect(reply.entries[0].peer_id).toBe((uint32_t)CLIENT_PEER);
            expect(reply.entries[0].channel).toBe(channel);
            expect(reply.entries[0].interface_id).toBe(interface_id);
            expect((const char *)reply.entries[0].agent_name).toBe("truck42");
            expect((const char *)reply.entries[0].interface_name).toBe("can0");
        });

        it("rejects forgetting an unknown pin", []() {
            AdminForgetMessage forget = { "ghost" };
            AdminForgetReplyMessage reply;
            uint8_t encoded[256];
            size_t encoded_size = AdminForgetMessage_Encode(&forget, encoded, sizeof(encoded));

            sendControlFrom(ADMIN_PEER, encoded, encoded_size);
            lastReply(&reply, AdminForgetReplyMessage_Decode);

            expect(reply.status).toBe(ADMIN_STATUS_UNKNOWN_AGENT);
        });
    });

    describe("echo fan-out", []() {
        beforeEach([]() {
            HubTransportPortMock_Reset(&transport);
            Broker_Init(&broker, &transport.port, NULL, NULL, false);
            events = Broker_Events(&broker);
            BrokerDriver_ConnectAgent(&events, &transport, AGENT_PEER, &truck_registration);
            BrokerDriver_ConnectClient(&events, CLIENT_PEER);
        });

        it("stamps injections towards the agent with the sender token", []() {
            uint32_t interface_id = BrokerDriver_InterfaceIdAt(&events, &transport, 0);
            uint8_t channel = BrokerDriver_OpenInterface(&events, &transport, CLIENT_PEER, interface_id, 0);
            FrameMessage frame = { 0x456, 2000, channel, 1, 0, 0, { 0x55 } };
            FrameMessage forwarded;
            MessageHeader header;
            uint8_t encoded[128];
            size_t encoded_size = FrameMessage_Encode(&frame, encoded, sizeof(encoded));

            events.on_peer_frame(events.context, CLIENT_PEER, encoded, encoded_size);
            MessageHeader_Decode(&header, transport.frame_log[0], transport.frame_sizes[0]);
            FrameMessage_Decode(&forwarded, transport.frame_log[0] + MESSAGE_HEADER_SIZE, header.length);

            expect(transport.frame_peers[0]).toBe((uint32_t)AGENT_PEER);
            expect(forwarded.route_flags).toBe(2 << FRAME_ROUTE_TOKEN_SHIFT);
        });

        it("fans an echo out to subscribers and strips the token", []() {
            uint32_t interface_id = BrokerDriver_InterfaceIdAt(&events, &transport, 0);
            FrameMessage echo = { 0x456, 2100, 0, 1, 0, 0, { 0x55 } };
            FrameMessage forwarded;
            MessageHeader header;
            uint8_t encoded[128];
            size_t encoded_size;

            BrokerDriver_OpenInterface(&events, &transport, CLIENT_PEER, interface_id, 0);
            echo.route_flags = FRAME_ROUTE_FLAG_ECHO | (2 << FRAME_ROUTE_TOKEN_SHIFT);
            encoded_size = FrameMessage_Encode(&echo, encoded, sizeof(encoded));

            events.on_peer_frame(events.context, AGENT_PEER, encoded, encoded_size);
            MessageHeader_Decode(&header, transport.frame_log[0], transport.frame_sizes[0]);
            FrameMessage_Decode(&forwarded, transport.frame_log[0] + MESSAGE_HEADER_SIZE, header.length);

            expect(transport.frame_count).toBe(1);
            expect(transport.frame_peers[0]).toBe((uint32_t)CLIENT_PEER);
            expect(forwarded.route_flags).toBe(FRAME_ROUTE_FLAG_ECHO);
        });

        it("suppresses the echo only for its opted-out originator", []() {
            uint32_t interface_id = BrokerDriver_InterfaceIdAt(&events, &transport, 0);
            FrameMessage echo = { 0x456, 2100, 0, 1, 0, 0, { 0x55 } };
            uint8_t encoded[128];
            size_t encoded_size;

            BrokerDriver_ConnectClient(&events, CLIENT_PEER + 1);
            BrokerDriver_OpenInterface(&events, &transport, CLIENT_PEER, interface_id, OPEN_FLAG_SUPPRESS_OWN_ECHO);
            BrokerDriver_OpenInterface(&events, &transport, CLIENT_PEER + 1, interface_id, 0);
            echo.route_flags = FRAME_ROUTE_FLAG_ECHO | (2 << FRAME_ROUTE_TOKEN_SHIFT);
            encoded_size = FrameMessage_Encode(&echo, encoded, sizeof(encoded));

            events.on_peer_frame(events.context, AGENT_PEER, encoded, encoded_size);

            expect(transport.frame_count).toBe(1);
            expect(transport.frame_peers[0]).toBe((uint32_t)(CLIENT_PEER + 1));
        });

        it("delivers the echo to an opted-out client when it is not the originator", []() {
            uint32_t interface_id = BrokerDriver_InterfaceIdAt(&events, &transport, 0);
            FrameMessage echo = { 0x456, 2100, 0, 1, 0, 0, { 0x55 } };
            uint8_t encoded[128];
            size_t encoded_size;

            BrokerDriver_ConnectClient(&events, CLIENT_PEER + 1);
            BrokerDriver_OpenInterface(&events, &transport, CLIENT_PEER, interface_id, OPEN_FLAG_SUPPRESS_OWN_ECHO);
            BrokerDriver_OpenInterface(&events, &transport, CLIENT_PEER + 1, interface_id, 0);
            echo.route_flags = FRAME_ROUTE_FLAG_ECHO | (3 << FRAME_ROUTE_TOKEN_SHIFT);
            encoded_size = FrameMessage_Encode(&echo, encoded, sizeof(encoded));

            events.on_peer_frame(events.context, AGENT_PEER, encoded, encoded_size);

            expect(transport.frame_count).toBe(2);
        });
    });

    describe("client write authorization", []() {
        static uint32_t can0_interface_id;

        beforeEach([]() {
            HubTransportPortMock_Reset(&transport);
            AuthorizationMock_Reset(&authorization);
            Broker_Init(&broker, &transport.port, NULL, &authorization.port, false);
            events = Broker_Events(&broker);
            BrokerDriver_ConnectAgent(&events, &transport, AGENT_PEER, &truck_registration);
            can0_interface_id = BrokerDriver_InterfaceIdAt(&events, &transport, 0);
        });

        it("drops an injected frame from a client without a write grant", []() {
            uint8_t channel;
            FrameMessage frame;
            uint8_t encoded[128];
            size_t encoded_size;

            connectClientWithFingerprint(CLIENT_PEER, TRUCK_FINGERPRINT);
            channel = openInterface(CLIENT_PEER, can0_interface_id, 0);
            HubTransportPortMock_Reset(&transport);
            frame = { 0x123, 1000, channel, 1, 0, 0, { 0x55 } };
            encoded_size = FrameMessage_Encode(&frame, encoded, sizeof(encoded));

            events.on_peer_frame(events.context, CLIENT_PEER, encoded, encoded_size);

            expect(transport.frame_count).toBe(0);
        });

        it("forwards an injected frame from a client with a write grant", []() {
            uint8_t channel;
            FrameMessage frame;
            uint8_t encoded[128];
            size_t encoded_size;

            AuthorizationMock_Grant(&authorization, TRUCK_FINGERPRINT, "truck42", "can0", true, true);
            connectClientWithFingerprint(CLIENT_PEER, TRUCK_FINGERPRINT);
            channel = openInterface(CLIENT_PEER, can0_interface_id, 0);
            HubTransportPortMock_Reset(&transport);
            frame = { 0x123, 1000, channel, 1, 0, 0, { 0x55 } };
            encoded_size = FrameMessage_Encode(&frame, encoded, sizeof(encoded));

            events.on_peer_frame(events.context, CLIENT_PEER, encoded, encoded_size);

            expect(transport.frame_count).toBe(1);
            expect(transport.frame_peers[0]).toBe((uint32_t)AGENT_PEER);
        });

        it("rejects OPEN with want_write when the client has no grant", []() {
            OpenMessage open = { can0_interface_id, OPEN_FLAG_WANT_WRITE };
            OpenAckMessage ack;
            MessageHeader header;
            uint8_t encoded[128];
            size_t encoded_size = OpenMessage_Encode(&open, encoded, sizeof(encoded));

            connectClientWithFingerprint(CLIENT_PEER, TRUCK_FINGERPRINT);
            HubTransportPortMock_Reset(&transport);
            events.on_peer_control(events.context, CLIENT_PEER, encoded, encoded_size, 0);
            MessageHeader_Decode(&header, transport.control_log[0], transport.control_sizes[0]);
            OpenAckMessage_Decode(&ack, transport.control_log[0] + MESSAGE_HEADER_SIZE, header.length);

            expect(ack.status).toBe(OPEN_STATUS_WRITE_DENIED);
        });

        it("allows OPEN with want_write once granted", []() {
            uint8_t channel;

            AuthorizationMock_Grant(&authorization, TRUCK_FINGERPRINT, "truck42", "can0", true, true);
            connectClientWithFingerprint(CLIENT_PEER, TRUCK_FINGERPRINT);
            channel = openInterface(CLIENT_PEER, can0_interface_id, OPEN_FLAG_WANT_WRITE);

            expect(channel != 0xFF).toBe(true);
        });

        it("rejects OPEN when read is denied for the client", []() {
            OpenMessage open = { can0_interface_id, 0 };
            OpenAckMessage ack;
            MessageHeader header;
            uint8_t encoded[128];
            size_t encoded_size = OpenMessage_Encode(&open, encoded, sizeof(encoded));

            AuthorizationMock_Grant(&authorization, "*", "truck42", "can0", false, false);
            connectClientWithFingerprint(CLIENT_PEER, TRUCK_FINGERPRINT);
            HubTransportPortMock_Reset(&transport);
            events.on_peer_control(events.context, CLIENT_PEER, encoded, encoded_size, 0);
            MessageHeader_Decode(&header, transport.control_log[0], transport.control_sizes[0]);
            OpenAckMessage_Decode(&ack, transport.control_log[0] + MESSAGE_HEADER_SIZE, header.length);

            expect(ack.status).toBe(OPEN_STATUS_READ_DENIED);
        });

        it("opens read-only when read is allowed but write is not", []() {
            uint8_t channel;

            connectClientWithFingerprint(CLIENT_PEER, TRUCK_FINGERPRINT);
            channel = openInterface(CLIENT_PEER, can0_interface_id, 0);

            expect(channel != 0xFF).toBe(true);
        });

        it("lets a plaintext client (no fingerprint) inject without a grant", []() {
            uint8_t channel;
            FrameMessage frame;
            uint8_t encoded[128];
            size_t encoded_size;

            BrokerDriver_ConnectClient(&events, CLIENT_PEER);
            channel = openInterface(CLIENT_PEER, can0_interface_id, 0);
            HubTransportPortMock_Reset(&transport);
            frame = { 0x123, 1000, channel, 1, 0, 0, { 0x55 } };
            encoded_size = FrameMessage_Encode(&frame, encoded, sizeof(encoded));

            events.on_peer_frame(events.context, CLIENT_PEER, encoded, encoded_size);

            expect(transport.frame_count).toBe(1);
        });
    });

    describe("admin acl plane", []() {
        beforeEach([]() {
            HubTransportPortMock_Reset(&transport);
            AuthorizationMock_Reset(&authorization);
            Broker_Init(&broker, &transport.port, NULL, &authorization.port, false);
            events = Broker_Events(&broker);
            BrokerDriver_ConnectAdmin(&events, ADMIN_PEER);
        });

        it("grants a write acl from the admin plane", []() {
            AdminAclSetMessage request = { "truck42", "can0", TRUCK_FINGERPRINT, 1, 1 };
            AdminAclSetReplyMessage reply;
            uint8_t reply_type;
            uint8_t encoded[256];
            size_t encoded_size = AdminAclSetMessage_Encode(&request, encoded, sizeof(encoded));

            sendControlFrom(ADMIN_PEER, encoded, encoded_size);
            reply_type = lastReply(&reply, AdminAclSetReplyMessage_Decode);

            expect(reply_type).toBe(kMESSAGE_TYPE_ADMIN_ACL_SET_REPLY);
            expect(reply.status).toBe(ADMIN_STATUS_OK);
            expect(authorization.port.write_allowed(authorization.port.context, TRUCK_FINGERPRINT, "truck42", "can0")).toBe(true);
        });

        it("revokes a write acl", []() {
            AdminAclRevokeMessage request = { "truck42", "can0", TRUCK_FINGERPRINT };
            AdminAclRevokeReplyMessage reply;
            uint8_t encoded[256];
            size_t encoded_size = AdminAclRevokeMessage_Encode(&request, encoded, sizeof(encoded));

            AuthorizationMock_Grant(&authorization, TRUCK_FINGERPRINT, "truck42", "can0", true, true);
            sendControlFrom(ADMIN_PEER, encoded, encoded_size);
            lastReply(&reply, AdminAclRevokeReplyMessage_Decode);

            expect(reply.status).toBe(ADMIN_STATUS_OK);
            expect(authorization.port.write_allowed(authorization.port.context, TRUCK_FINGERPRINT, "truck42", "can0")).toBe(false);
        });

        it("lists acl entries", []() {
            AdminAclListMessage request = { 0 };
            AdminAclListReplyMessage reply;
            uint8_t reply_type;
            uint8_t encoded[128];
            size_t encoded_size = AdminAclListMessage_Encode(&request, encoded, sizeof(encoded));

            AuthorizationMock_Grant(&authorization, TRUCK_FINGERPRINT, "truck42", "can0", true, true);
            sendControlFrom(ADMIN_PEER, encoded, encoded_size);
            reply_type = lastReply(&reply, AdminAclListReplyMessage_Decode);

            expect(reply_type).toBe(kMESSAGE_TYPE_ADMIN_ACL_LIST_REPLY);
            expect(reply.count).toBe(1);
            expect((const char *)reply.entries[0].interface_name).toBe("can0");
            expect(reply.entries[0].can_write).toBe(1);
        });
    });

    describe("backpressure", []() {
        beforeEach([]() {
            uint32_t interface_id;

            HubTransportPortMock_Reset(&transport);
            Broker_Init(&broker, &transport.port, NULL, NULL, false);
            events = Broker_Events(&broker);
            BrokerDriver_ConnectAgent(&events, &transport, AGENT_PEER, &truck_registration);
            BrokerDriver_ConnectClient(&events, CLIENT_PEER);
            BrokerDriver_ConnectAdmin(&events, ADMIN_PEER);
            interface_id = BrokerDriver_InterfaceIdAt(&events, &transport, 0);
            client_channel = BrokerDriver_OpenInterface(&events, &transport, CLIENT_PEER, interface_id, 0);
        });

        it("queues a refused frame and drains it when the peer is writable", []() {
            FrameMessage frame = { 0x123, 1000, 0, 1, 0, 0, { 0x55 } };
            AdminPeersMessage request = { 0 };
            AdminPeersReplyMessage reply;
            uint8_t frame_encoded[128];
            uint8_t encoded[128];
            size_t frame_size = FrameMessage_Encode(&frame, frame_encoded, sizeof(frame_encoded));
            size_t encoded_size = AdminPeersMessage_Encode(&request, encoded, sizeof(encoded));

            events.on_peer_frame(events.context, AGENT_PEER, frame_encoded, frame_size);
            transport.frame_result = false;
            events.on_peer_frame(events.context, AGENT_PEER, frame_encoded, frame_size);
            sendControlFrom(ADMIN_PEER, encoded, encoded_size);
            lastReply(&reply, AdminPeersReplyMessage_Decode);

            expect(reply.entries[1].peer_id).toBe((uint32_t)CLIENT_PEER);
            expect(reply.entries[1].frames_forwarded).toBe((uint32_t)1);
            expect(reply.entries[1].frames_dropped).toBe((uint32_t)0);

            transport.frame_result = true;
            events.on_peer_writable(events.context, CLIENT_PEER);
            sendControlFrom(ADMIN_PEER, encoded, encoded_size);
            lastReply(&reply, AdminPeersReplyMessage_Decode);

            expect(reply.entries[1].frames_forwarded).toBe((uint32_t)2);
            expect(reply.entries[1].frames_dropped).toBe((uint32_t)0);
        });

        it("counts per-channel drops when a channel overflows", []() {
            FrameMessage frame = { 0x123, 1000, 0, 1, 0, 0, { 0x55 } };
            AdminClientsMessage request = { 0, "truck42" };
            AdminClientsReplyMessage reply;
            uint8_t frame_encoded[128];
            uint8_t encoded[256];
            size_t frame_size = FrameMessage_Encode(&frame, frame_encoded, sizeof(frame_encoded));
            size_t encoded_size = AdminClientsMessage_Encode(&request, encoded, sizeof(encoded));
            int i;

            transport.frame_result = false;
            for(i=0; i<EGRESS_QUEUE_CHANNEL_CAP + 5; i++) {
                events.on_peer_frame(events.context, AGENT_PEER, frame_encoded, frame_size);
            }
            sendControlFrom(ADMIN_PEER, encoded, encoded_size);
            lastReply(&reply, AdminClientsReplyMessage_Decode);

            expect(reply.entries[0].peer_id).toBe((uint32_t)CLIENT_PEER);
            expect(reply.entries[0].frames_forwarded).toBe((uint32_t)0);
            expect(reply.entries[0].frames_dropped).toBe((uint32_t)5);
        });

        it("aggregates frame totals in the status reply", []() {
            FrameMessage routable = { 0x123, 1000, 0, 1, 0, 0, { 0x55 } };
            FrameMessage unroutable = { 0x456, 1000, 1, 1, 0, 0, { 0x55 } };
            AdminStatusReplyMessage reply;
            uint8_t frame_encoded[128];
            uint8_t encoded[128];
            size_t routable_size = FrameMessage_Encode(&routable, frame_encoded, sizeof(frame_encoded));
            size_t encoded_size = AdminStatusMessage_Encode(encoded, sizeof(encoded));

            events.on_peer_frame(events.context, AGENT_PEER, frame_encoded, routable_size);
            transport.frame_result = false;
            events.on_peer_frame(events.context, AGENT_PEER, frame_encoded, routable_size);
            transport.frame_result = true;
            routable_size = FrameMessage_Encode(&unroutable, frame_encoded, sizeof(frame_encoded));
            events.on_peer_frame(events.context, AGENT_PEER, frame_encoded, routable_size);
            sendControlFrom(ADMIN_PEER, encoded, encoded_size);
            lastReply(&reply, AdminStatusReplyMessage_Decode);

            expect(reply.frames_received).toBe((uint64_t)3);
            expect(reply.frames_forwarded).toBe((uint64_t)1);
            expect(reply.frames_dropped).toBe((uint64_t)0);
            expect(reply.frames_unroutable).toBe((uint64_t)1);
        });

        it("lists interfaces with their traffic through the admin plane", []() {
            FrameMessage frame = { 0x123, 1000, 0, 1, 0, 0, { 0x55 } };
            AdminInterfacesMessage request = { 0 };
            AdminInterfacesReplyMessage reply;
            uint8_t reply_type;
            uint8_t frame_encoded[128];
            uint8_t encoded[128];
            size_t frame_size = FrameMessage_Encode(&frame, frame_encoded, sizeof(frame_encoded));
            size_t encoded_size = AdminInterfacesMessage_Encode(&request, encoded, sizeof(encoded));

            events.on_peer_frame(events.context, AGENT_PEER, frame_encoded, frame_size);
            sendControlFrom(ADMIN_PEER, encoded, encoded_size);
            reply_type = lastReply(&reply, AdminInterfacesReplyMessage_Decode);

            expect(reply_type).toBe(kMESSAGE_TYPE_ADMIN_INTERFACES_REPLY);
            expect(reply.count).toBe(2);
            expect(reply.entries[0].subscriber_count).toBe(1);
            expect(reply.entries[0].frames_received).toBe((uint64_t)1);
            expect((const char *)reply.entries[0].agent_name).toBe("truck42");
            expect((const char *)reply.entries[0].interface_name).toBe("can0");
            expect(reply.entries[1].subscriber_count).toBe(0);
        });

        it("evicts a peer whose control send fails", []() {
            ListMessage list = { 0 };
            uint8_t encoded[128];
            size_t encoded_size = ListMessage_Encode(&list, encoded, sizeof(encoded));

            transport.control_result = false;
            sendControlFrom(CLIENT_PEER, encoded, encoded_size);

            expect(transport.close_count).toBe(1);
            expect(transport.last_closed_peer).toBe((uint32_t)CLIENT_PEER);
        });

        it("evicts an unknown peer that misses the hello deadline telling it why", []() {
            ErrorMessage error;
            uint8_t error_type;

            connectPeer(999, NULL, false);

            Broker_Tick(&broker, 0);
            Broker_Tick(&broker, 4999999);
            expect(transport.close_count).toBe(0);

            Broker_Tick(&broker, 5000000);
            error_type = lastReply(&error, ErrorMessage_Decode);
            expect(error_type).toBe(kMESSAGE_TYPE_ERROR);
            expect(error.code).toBe(kERROR_CODE_HELLO_TIMEOUT);
            expect(transport.close_count).toBe(1);
            expect(transport.last_closed_peer).toBe((uint32_t)999);
        });

        it("never evicts peers that declared a role", []() {
            Broker_Tick(&broker, 0);
            Broker_Tick(&broker, 60000000);

            expect(transport.close_count).toBe(0);
        });
    });

    describe("interface configuration", []() {
        beforeEach([]() {
            HubTransportPortMock_Reset(&transport);
            Broker_Init(&broker, &transport.port, NULL, NULL, false);
            events = Broker_Events(&broker);
            BrokerDriver_ConnectAgent(&events, &transport, AGENT_PEER, &truck_registration);
            BrokerDriver_ConnectAdmin(&events, ADMIN_PEER);
        });

        it("forwards an admin request to the owning agent", []() {
            AdminIfconfigMessage request;
            IfconfigMessage forwarded;
            MessageHeader header;
            uint8_t encoded[256];
            size_t encoded_size;

            memset(&request, 0, sizeof(request));
            snprintf(request.agent_name, sizeof(request.agent_name), "truck42");
            snprintf(request.interface_name, sizeof(request.interface_name), "can0");
            request.op = IFCONFIG_OP_SET_BITRATE;
            request.bitrate = 500000;
            encoded_size = AdminIfconfigMessage_Encode(&request, encoded, sizeof(encoded));
            sendControlFrom(ADMIN_PEER, encoded, encoded_size);

            MessageHeader_Decode(&header, transport.control_log[0], transport.control_sizes[0]);
            IfconfigMessage_Decode(&forwarded, transport.control_log[0] + MESSAGE_HEADER_SIZE, header.length);

            expect(transport.control_count).toBe(1);
            expect(transport.control_peers[0]).toBe((uint32_t)AGENT_PEER);
            expect(header.type).toBe(kMESSAGE_TYPE_IFCONFIG);
            expect((const char *)forwarded.interface_name).toBe("can0");
            expect(forwarded.op).toBe(IFCONFIG_OP_SET_BITRATE);
            expect(forwarded.bitrate).toBe((uint32_t)500000);
        });

        it("relays the agent reply back to the waiting admin", []() {
            AdminIfconfigMessage request;
            IfconfigReplyMessage agent_reply = { "can0", IFCONFIG_STATUS_OK };
            AdminIfconfigReplyMessage admin_reply;
            uint8_t reply_type;
            uint8_t encoded[256];
            size_t encoded_size;

            memset(&request, 0, sizeof(request));
            snprintf(request.agent_name, sizeof(request.agent_name), "truck42");
            snprintf(request.interface_name, sizeof(request.interface_name), "can0");
            request.op = IFCONFIG_OP_UP;
            encoded_size = AdminIfconfigMessage_Encode(&request, encoded, sizeof(encoded));
            sendControlFrom(ADMIN_PEER, encoded, encoded_size);

            encoded_size = IfconfigReplyMessage_Encode(&agent_reply, encoded, sizeof(encoded));
            sendControlFrom(AGENT_PEER, encoded, encoded_size);
            reply_type = lastReply(&admin_reply, AdminIfconfigReplyMessage_Decode);

            expect(transport.control_peers[transport.control_count - 1]).toBe((uint32_t)ADMIN_PEER);
            expect(reply_type).toBe(kMESSAGE_TYPE_ADMIN_IFCONFIG_REPLY);
            expect(admin_reply.status).toBe(ADMIN_IFCONFIG_STATUS_OK);
        });

        it("answers unknown interface without reaching the agent", []() {
            AdminIfconfigMessage request;
            AdminIfconfigReplyMessage reply;
            uint8_t reply_type;
            uint8_t encoded[256];
            size_t encoded_size;

            memset(&request, 0, sizeof(request));
            snprintf(request.agent_name, sizeof(request.agent_name), "truck42");
            snprintf(request.interface_name, sizeof(request.interface_name), "can9");
            request.op = IFCONFIG_OP_DOWN;
            encoded_size = AdminIfconfigMessage_Encode(&request, encoded, sizeof(encoded));
            sendControlFrom(ADMIN_PEER, encoded, encoded_size);
            reply_type = lastReply(&reply, AdminIfconfigReplyMessage_Decode);

            expect(transport.control_count).toBe(1);
            expect(transport.control_peers[0]).toBe((uint32_t)ADMIN_PEER);
            expect(reply_type).toBe(kMESSAGE_TYPE_ADMIN_IFCONFIG_REPLY);
            expect(reply.status).toBe(ADMIN_IFCONFIG_STATUS_UNKNOWN_INTERFACE);
        });

        it("answers unreachable when the agent drops before replying", []() {
            AdminIfconfigMessage request;
            AdminIfconfigReplyMessage reply;
            uint8_t reply_type;
            uint8_t encoded[256];
            size_t encoded_size;

            memset(&request, 0, sizeof(request));
            snprintf(request.agent_name, sizeof(request.agent_name), "truck42");
            snprintf(request.interface_name, sizeof(request.interface_name), "can0");
            request.op = IFCONFIG_OP_SET_BITRATE;
            request.bitrate = 250000;
            encoded_size = AdminIfconfigMessage_Encode(&request, encoded, sizeof(encoded));
            sendControlFrom(ADMIN_PEER, encoded, encoded_size);

            events.on_peer_disconnected(events.context, AGENT_PEER, 0);
            reply_type = lastReply(&reply, AdminIfconfigReplyMessage_Decode);

            expect(transport.control_peers[transport.control_count - 1]).toBe((uint32_t)ADMIN_PEER);
            expect(reply_type).toBe(kMESSAGE_TYPE_ADMIN_IFCONFIG_REPLY);
            expect(reply.status).toBe(ADMIN_IFCONFIG_STATUS_AGENT_UNREACHABLE);
        });
    });
});
