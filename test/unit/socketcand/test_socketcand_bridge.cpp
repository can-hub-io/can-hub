#include <cest>

#include <cstring>

extern "C" {
#include "protocol/frame_message.h"
#include "protocol/list_message.h"
#include "protocol/message_header.h"
#include "protocol/open_message.h"
#include "socketcand/socketcand_bridge.h"
#include "socketcand_server_port_mock.h"
#include "transport_port_mock.h"
}

static SocketcandBridge bridge;
static TransportPortMock hub;
static SocketcandServerPortMock server;
static TransportEvents hub_events;
static SocketcandServerEvents server_events;

static void clientBytes(uint32_t connection_id, const char *text)
{
    server_events.on_client_bytes(server_events.context, connection_id, (const uint8_t *)text, strlen(text));
}

static void feedListReplyOneBus()
{
    ListReplyMessage reply;
    uint8_t wire[256];
    size_t size;

    memset(&reply, 0, sizeof(reply));
    reply.count = 1;
    reply.entries[0].interface_id = 7;
    strncpy(reply.entries[0].agent_name, "truck42", REGISTER_AGENT_NAME_SIZE - 1);
    strncpy(reply.entries[0].interface_name, "can0", REGISTER_INTERFACE_NAME_SIZE - 1);
    size = ListReplyMessage_Encode(&reply, wire, sizeof(wire));
    hub_events.on_control(hub_events.context, wire, size, 1000);
}

static void feedOpenAck(uint8_t status, uint8_t channel)
{
    OpenAckMessage ack = { status, channel, 7 };
    uint8_t wire[64];
    size_t size = OpenAckMessage_Encode(&ack, wire, sizeof(wire));

    hub_events.on_control(hub_events.context, wire, size, 2000);
}

static void bringUpReady()
{
    SocketcandBridge_Tick(&bridge, 0);
    hub_events.on_connected(hub_events.context);
    feedListReplyOneBus();
}

static void feedOpenAckFor(uint8_t status, uint8_t channel, uint32_t interface_id)
{
    OpenAckMessage ack = { status, channel, interface_id };
    uint8_t wire[64];
    size_t size = OpenAckMessage_Encode(&ack, wire, sizeof(wire));

    hub_events.on_control(hub_events.context, wire, size, 2000);
}

static void relistOneBus(const char *agent, const char *interface, uint32_t interface_id, uint64_t now_us)
{
    ListReplyMessage reply;
    uint8_t wire[256];
    size_t size;

    SocketcandBridge_Tick(&bridge, now_us);

    memset(&reply, 0, sizeof(reply));
    reply.count = 1;
    reply.entries[0].interface_id = interface_id;
    strncpy(reply.entries[0].agent_name, agent, REGISTER_AGENT_NAME_SIZE - 1);
    strncpy(reply.entries[0].interface_name, interface, REGISTER_INTERFACE_NAME_SIZE - 1);
    size = ListReplyMessage_Encode(&reply, wire, sizeof(wire));
    hub_events.on_control(hub_events.context, wire, size, now_us);
}

static void openRawAndAck(uint32_t connection_id)
{
    server_events.on_client_connected(server_events.context, connection_id);
    clientBytes(connection_id, "< open truck42/can0 >");
    feedOpenAckFor(OPEN_STATUS_OK, 5, 7);
    clientBytes(connection_id, "< rawmode >");
}

static void feedFrameOnChannel(uint8_t channel)
{
    FrameMessage frame;
    uint8_t wire[128];
    size_t size;

    memset(&frame, 0, sizeof(frame));
    frame.channel = channel;
    frame.can_id = 0x123;
    frame.payload_length = 2;
    frame.payload[0] = 0xDE;
    frame.payload[1] = 0xAD;
    frame.timestamp_us = 1000000;
    size = FrameMessage_Encode(&frame, wire, sizeof(wire));
    hub_events.on_frame(hub_events.context, wire, size);
}

describe("socketcand_bridge", []() {
    beforeEach([]() {
        TransportPortMock_Reset(&hub);
        SocketcandServerPortMock_Reset(&server);
        SocketcandBridge_Init(&bridge, &hub.port, &server.port, "host", "can://127.0.0.1:29536", true);
        hub_events = SocketcandBridge_TransportEvents(&bridge);
        server_events = SocketcandBridge_ServerEvents(&bridge);
    });

    it("connects to the hub then sends HELLO and LIST", []() {
        SocketcandBridge_Tick(&bridge, 0);
        hub_events.on_connected(hub_events.context);

        expect(hub.connect_calls).toBe(1);
        expect(hub.control_count).toBe(2);
        expect(SocketcandBridge_HubState(&bridge)).toBe((uint8_t)kSOCKETCAND_HUB_READY);
    });

    it("greets a new socketcand client", []() {
        bringUpReady();
        server_events.on_client_connected(server_events.context, 1);

        expect(strstr(SocketcandServerPortMock_Written(&server, 1), "< hi >") != nullptr).toBe(true);
    });

    it("opens a bus by namespaced name and acks ok", []() {
        MessageHeader header;
        OpenMessage open;

        bringUpReady();
        server_events.on_client_connected(server_events.context, 1);
        clientBytes(1, "< open truck42/can0 >");

        MessageHeader_Decode(&header, hub.control_log[2], hub.control_sizes[2]);
        OpenMessage_Decode(&open, hub.control_log[2] + MESSAGE_HEADER_SIZE, header.length);

        expect(header.type).toBe((uint8_t)kMESSAGE_TYPE_OPEN);
        expect(open.interface_id).toBe((uint32_t)7);
        expect((open.flags & OPEN_FLAG_WANT_WRITE) != 0).toBe(true);

        feedOpenAck(OPEN_STATUS_OK, 5);
        expect(strstr(SocketcandServerPortMock_Written(&server, 1), "< ok >") != nullptr).toBe(true);
    });

    it("delivers hub frames to a client in raw mode", []() {
        FrameMessage frame;
        uint8_t wire[128];
        size_t size;

        bringUpReady();
        server_events.on_client_connected(server_events.context, 1);
        clientBytes(1, "< open truck42/can0 >");
        feedOpenAck(OPEN_STATUS_OK, 5);
        clientBytes(1, "< rawmode >");

        memset(&frame, 0, sizeof(frame));
        frame.channel = 5;
        frame.can_id = 0x123;
        frame.payload_length = 2;
        frame.payload[0] = 0xDE;
        frame.payload[1] = 0xAD;
        frame.timestamp_us = 1000000;
        size = FrameMessage_Encode(&frame, wire, sizeof(wire));
        hub_events.on_frame(hub_events.context, wire, size);

        expect(strstr(SocketcandServerPortMock_Written(&server, 1), "< frame 123 1.000000 DEAD >") != nullptr).toBe(true);
    });

    it("acks rawmode with < ok > before delivering frames", []() {
        bringUpReady();
        server_events.on_client_connected(server_events.context, 1);
        clientBytes(1, "< open truck42/can0 >");
        feedOpenAck(OPEN_STATUS_OK, 5);
        SocketcandServerPortMock_Reset(&server);
        clientBytes(1, "< rawmode >");

        expect(strstr(SocketcandServerPortMock_Written(&server, 1), "< ok >") != nullptr).toBe(true);
    });

    it("forwards a client send to the hub when writable", []() {
        MessageHeader header;
        FrameMessage frame;

        bringUpReady();
        server_events.on_client_connected(server_events.context, 1);
        clientBytes(1, "< open truck42/can0 >");
        feedOpenAck(OPEN_STATUS_OK, 5);
        clientBytes(1, "< rawmode >");
        clientBytes(1, "< send 123 2 de ad >");

        MessageHeader_Decode(&header, hub.last_frame, hub.last_frame_size);
        FrameMessage_Decode(&frame, hub.last_frame + MESSAGE_HEADER_SIZE, header.length);

        expect(hub.frame_count).toBe(1);
        expect(frame.channel).toBe(5);
        expect(frame.can_id & FRAME_CAN_ID_MASK).toBe((uint32_t)0x123);
        expect(frame.payload[0]).toBe(0xDE);
    });

    it("retries read-only when write is denied, then rejects sends", []() {
        MessageHeader header;
        OpenMessage reopen;

        bringUpReady();
        server_events.on_client_connected(server_events.context, 1);
        clientBytes(1, "< open truck42/can0 >");
        feedOpenAck(OPEN_STATUS_WRITE_DENIED, 0);

        MessageHeader_Decode(&header, hub.control_log[3], hub.control_sizes[3]);
        OpenMessage_Decode(&reopen, hub.control_log[3] + MESSAGE_HEADER_SIZE, header.length);
        expect((reopen.flags & OPEN_FLAG_WANT_WRITE) == 0).toBe(true);

        feedOpenAck(OPEN_STATUS_OK, 6);
        clientBytes(1, "< rawmode >");
        clientBytes(1, "< send 123 1 ff >");

        expect(hub.frame_count).toBe(0);
        expect(strstr(SocketcandServerPortMock_Written(&server, 1), "write not permitted") != nullptr).toBe(true);
    });

    it("closes the client when the bus is unknown", []() {
        bringUpReady();
        server_events.on_client_connected(server_events.context, 1);
        clientBytes(1, "< open truck42/can9 >");

        expect(strstr(SocketcandServerPortMock_Written(&server, 1), "< error") != nullptr).toBe(true);
        expect(SocketcandServerPortMock_Closed(&server, 1)).toBe(true);
    });

    it("emits a beacon on tick once ready", []() {
        bringUpReady();
        SocketcandBridge_Tick(&bridge, 4000000);

        expect(server.beacon_count > 0).toBe(true);
        expect(strstr(server.last_beacon, "<Bus name=\"truck42/can0\"/>") != nullptr).toBe(true);
    });

    it("re-opens against the new interface id when the agent renews", []() {
        MessageHeader header;
        OpenMessage reopen;

        bringUpReady();
        openRawAndAck(1);

        relistOneBus("truck42", "can0", 99, 6000000);

        expect(hub.control_count).toBe(5);
        MessageHeader_Decode(&header, hub.control_log[4], hub.control_sizes[4]);
        OpenMessage_Decode(&reopen, hub.control_log[4] + MESSAGE_HEADER_SIZE, header.length);
        expect(header.type).toBe((uint8_t)kMESSAGE_TYPE_OPEN);
        expect(reopen.interface_id).toBe((uint32_t)99);
        expect((reopen.flags & OPEN_FLAG_WANT_WRITE) != 0).toBe(true);
    });

    it("reattaches transparently without re-sending ok or dropping the client", []() {
        bringUpReady();
        openRawAndAck(1);

        relistOneBus("truck42", "can0", 99, 6000000);
        SocketcandServerPortMock_Reset(&server);
        feedOpenAckFor(OPEN_STATUS_OK, 8, 99);

        expect(strstr(SocketcandServerPortMock_Written(&server, 1), "< ok >") == nullptr).toBe(true);
        expect(SocketcandServerPortMock_Closed(&server, 1)).toBe(false);

        feedFrameOnChannel(8);
        expect(strstr(SocketcandServerPortMock_Written(&server, 1), "< frame 123 1.000000 DEAD >") != nullptr).toBe(true);
    });

    it("does not reattach while the renewed interface is absent", []() {
        bringUpReady();
        openRawAndAck(1);

        relistOneBus("truck42", "can9", 99, 6000000);

        expect(hub.control_count).toBe(4);
        expect(SocketcandServerPortMock_Closed(&server, 1)).toBe(false);
    });

    it("drops client sends silently during the reattach window", []() {
        bringUpReady();
        openRawAndAck(1);

        relistOneBus("truck42", "can0", 99, 6000000);
        SocketcandServerPortMock_Reset(&server);
        clientBytes(1, "< send 123 1 ff >");

        expect(hub.frame_count).toBe(0);
        expect(SocketcandServerPortMock_Written(&server, 1)[0]).toBe('\0');
    });
});
