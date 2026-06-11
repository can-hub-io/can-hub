#include <cest>

#include <cstdio>
#include <cstring>

extern "C" {
#include "client/client.h"
#include "protocol/frame_message.h"
#include "protocol/list_message.h"
#include "protocol/message_header.h"
#include "protocol/open_message.h"
#include "protocol/subscribe_message.h"
#include "transport_port_mock.h"
}

#define TEST_INTERFACE_ID 7

static Client client;
static TransportPortMock hub;
static TransportEvents hub_events;
static ListReplyMessage last_list_reply;
static int list_reply_count;
static uint8_t last_open_status;
static uint8_t last_open_channel;
static uint32_t last_open_interface_id;
static int open_result_count;
static FrameMessage last_frame;
static int frame_count;
static uint16_t last_error_code;
static char last_error_detail[ERROR_DETAIL_SIZE];
static int error_count;
static int disconnect_count;

static ClientEvents clientEvents();
static void resetCallbackLog();
static void connect();
static void feedListReply(uint8_t flags, const char *agent, const char *iface, uint32_t interface_id);
static void feedOpenAck(uint8_t status, uint8_t channel);
static void feedHubFrame(uint8_t channel, uint32_t can_id, uint8_t byte0);
static void feedHubError(uint16_t code, const char *detail);
static uint8_t sentMessageType(uint8_t slot);
static void decodeSentOpen(uint8_t slot, OpenMessage *open);
static void decodeSentList(uint8_t slot, ListMessage *list);
static void decodeSentSubscribe(uint8_t slot, SubscribeMessage *subscribe);
static void decodeSentFrame(FrameMessage *frame);
static void onListReply(void *context, const ListReplyMessage *reply);
static void onOpenResult(void *context, uint8_t status, uint8_t channel, uint32_t interface_id);
static void onFrame(void *context, const FrameMessage *frame);
static void onError(void *context, uint16_t code, const char *detail);
static void onDisconnected(void *context);

describe("client", []() {
    beforeEach([]() {
        ClientEvents events = clientEvents();

        TransportPortMock_Reset(&hub);
        resetCallbackLog();
        Client_Init(&client, &hub.port, &events);
        hub_events = Client_TransportEvents(&client);
    });

    it("sends HELLO on connect", []() {
        connect();

        expect(hub.control_count).toBe(1);
        expect(sentMessageType(0)).toBe((uint8_t)kMESSAGE_TYPE_HELLO);
        expect(Client_State(&client)).toBe((uint8_t)kCLIENT_READY);
    });

    it("runs a list requested before connect after HELLO", []() {
        ListMessage list;

        Client_RequestList(&client, 0);
        connect();
        decodeSentList(1, &list);

        expect(hub.control_count).toBe(2);
        expect(sentMessageType(1)).toBe((uint8_t)kMESSAGE_TYPE_LIST);
        expect(list.offset).toBe((uint16_t)0);
    });

    it("emits list replies to the consumer", []() {
        Client_RequestList(&client, 0);
        connect();
        feedListReply(0, "truck42", "can0", TEST_INTERFACE_ID);

        expect(list_reply_count).toBe(1);
        expect(last_list_reply.count).toBe((uint8_t)1);
        expect(last_list_reply.entries[0].interface_id).toBe((uint32_t)TEST_INTERFACE_ID);
    });

    it("opens by id after connect", []() {
        OpenMessage open;

        Client_OpenById(&client, TEST_INTERFACE_ID, OPEN_FLAG_WANT_WRITE);
        connect();
        decodeSentOpen(1, &open);

        expect(sentMessageType(1)).toBe((uint8_t)kMESSAGE_TYPE_OPEN);
        expect(open.interface_id).toBe((uint32_t)TEST_INTERFACE_ID);
        expect((open.flags & OPEN_FLAG_WANT_WRITE) != 0).toBe(true);
    });

    it("reports the channel and enters open on OPEN_ACK ok", []() {
        Client_OpenById(&client, TEST_INTERFACE_ID, 0);
        connect();
        feedOpenAck(OPEN_STATUS_OK, 5);

        expect(open_result_count).toBe(1);
        expect(last_open_status).toBe((uint8_t)OPEN_STATUS_OK);
        expect(last_open_channel).toBe((uint8_t)5);
        expect(Client_State(&client)).toBe((uint8_t)kCLIENT_OPEN);
        expect(Client_Channel(&client)).toBe((uint8_t)5);
    });

    it("reports a rejected open and returns to ready", []() {
        Client_OpenById(&client, TEST_INTERFACE_ID, 0);
        connect();
        feedOpenAck(OPEN_STATUS_REJECTED, 0);

        expect(open_result_count).toBe(1);
        expect(last_open_status).toBe((uint8_t)OPEN_STATUS_REJECTED);
        expect(Client_State(&client)).toBe((uint8_t)kCLIENT_READY);
    });

    it("resolves a namespaced name via LIST then opens", []() {
        OpenMessage open;

        Client_OpenByName(&client, "truck42/can0", 0);
        connect();
        feedListReply(0, "truck42", "can0", TEST_INTERFACE_ID);
        decodeSentOpen(2, &open);

        expect(sentMessageType(1)).toBe((uint8_t)kMESSAGE_TYPE_LIST);
        expect(sentMessageType(2)).toBe((uint8_t)kMESSAGE_TYPE_OPEN);
        expect(open.interface_id).toBe((uint32_t)TEST_INTERFACE_ID);
    });

    it("paginates LIST until the name is found", []() {
        ListMessage second_page;

        Client_OpenByName(&client, "truck42/can1", 0);
        connect();
        feedListReply(LIST_REPLY_FLAG_MORE, "truck42", "can0", 5);
        decodeSentList(2, &second_page);
        feedListReply(0, "truck42", "can1", TEST_INTERFACE_ID);
        feedOpenAck(OPEN_STATUS_OK, 3);

        expect(second_page.offset).toBe((uint16_t)1);
        expect(Client_State(&client)).toBe((uint8_t)kCLIENT_OPEN);
    });

    it("reports not-found when resolution exhausts the catalogue", []() {
        Client_OpenByName(&client, "truck42/can9", 0);
        connect();
        feedListReply(0, "truck42", "can0", TEST_INTERFACE_ID);

        expect(error_count).toBe(1);
        expect(last_error_code).toBe((uint16_t)CLIENT_ERROR_INTERFACE_NOT_FOUND);
        expect(strcmp(last_error_detail, "truck42/can9") == 0).toBe(true);
        expect(Client_State(&client)).toBe((uint8_t)kCLIENT_READY);
    });

    it("sends the stored filters after the open succeeds", []() {
        CanFilter filter = { 0x123, 0x7FF };
        SubscribeMessage subscribe;

        Client_SetFilters(&client, &filter, 1);
        Client_OpenById(&client, TEST_INTERFACE_ID, 0);
        connect();
        feedOpenAck(OPEN_STATUS_OK, 5);
        decodeSentSubscribe(2, &subscribe);

        expect(sentMessageType(2)).toBe((uint8_t)kMESSAGE_TYPE_SUBSCRIBE);
        expect(subscribe.channel).toBe((uint8_t)5);
        expect(subscribe.filter_count).toBe((uint8_t)1);
        expect(subscribe.filters[0].can_id).toBe((uint32_t)0x123);
    });

    it("stamps the open channel on sent frames", []() {
        FrameMessage frame;
        FrameMessage sent;

        Client_OpenById(&client, TEST_INTERFACE_ID, OPEN_FLAG_WANT_WRITE);
        connect();
        feedOpenAck(OPEN_STATUS_OK, 9);
        memset(&frame, 0, sizeof(frame));
        frame.can_id = 0x321;
        frame.payload_length = 1;
        frame.payload[0] = 0xBE;
        Client_SendFrame(&client, &frame);
        decodeSentFrame(&sent);

        expect(hub.frame_count).toBe(1);
        expect(sent.channel).toBe((uint8_t)9);
        expect(sent.can_id).toBe((uint32_t)0x321);
    });

    it("refuses to send frames before the channel is open", []() {
        FrameMessage frame;

        memset(&frame, 0, sizeof(frame));
        connect();

        expect(Client_SendFrame(&client, &frame)).toBe(false);
        expect(hub.frame_count).toBe(0);
    });

    it("emits hub frames to the consumer while open", []() {
        Client_OpenById(&client, TEST_INTERFACE_ID, 0);
        connect();
        feedOpenAck(OPEN_STATUS_OK, 5);
        feedHubFrame(5, 0x123, 0xDE);

        expect(frame_count).toBe(1);
        expect(last_frame.can_id).toBe((uint32_t)0x123);
        expect(last_frame.payload[0]).toBe((uint8_t)0xDE);
    });

    it("drops hub frames before the channel is open", []() {
        connect();
        feedHubFrame(5, 0x123, 0xDE);

        expect(frame_count).toBe(0);
    });

    it("emits hub errors to the consumer", []() {
        connect();
        feedHubError(2, "role rejected");

        expect(error_count).toBe(1);
        expect(last_error_code).toBe((uint16_t)2);
        expect(strcmp(last_error_detail, "role rejected") == 0).toBe(true);
    });

    it("emits disconnect and returns to disconnected", []() {
        connect();
        hub_events.on_disconnected(hub_events.context, 2000);

        expect(disconnect_count).toBe(1);
        expect(Client_State(&client)).toBe((uint8_t)kCLIENT_DISCONNECTED);
    });
});

static ClientEvents clientEvents()
{
    ClientEvents events = {
        nullptr,
        onListReply,
        onOpenResult,
        onFrame,
        onError,
        onDisconnected,
    };

    return events;
}

static void resetCallbackLog()
{
    memset(&last_list_reply, 0, sizeof(last_list_reply));
    list_reply_count = 0;
    last_open_status = 0;
    last_open_channel = 0;
    last_open_interface_id = 0;
    open_result_count = 0;
    memset(&last_frame, 0, sizeof(last_frame));
    frame_count = 0;
    last_error_code = 0;
    memset(last_error_detail, 0, sizeof(last_error_detail));
    error_count = 0;
    disconnect_count = 0;
}

static void connect()
{
    hub_events.on_connected(hub_events.context);
}

static void feedListReply(uint8_t flags, const char *agent, const char *iface, uint32_t interface_id)
{
    ListReplyMessage reply;
    uint8_t wire[256];
    size_t size;

    memset(&reply, 0, sizeof(reply));
    reply.count = 1;
    reply.flags = flags;
    reply.entries[0].interface_id = interface_id;
    snprintf(reply.entries[0].agent_name, REGISTER_AGENT_NAME_SIZE, "%s", agent);
    snprintf(reply.entries[0].interface_name, REGISTER_INTERFACE_NAME_SIZE, "%s", iface);
    size = ListReplyMessage_Encode(&reply, wire, sizeof(wire));
    hub_events.on_control(hub_events.context, wire, size, 1000);
}

static void feedOpenAck(uint8_t status, uint8_t channel)
{
    OpenAckMessage ack = { status, channel, TEST_INTERFACE_ID };
    uint8_t wire[64];
    size_t size = OpenAckMessage_Encode(&ack, wire, sizeof(wire));

    hub_events.on_control(hub_events.context, wire, size, 1000);
}

static void feedHubFrame(uint8_t channel, uint32_t can_id, uint8_t byte0)
{
    FrameMessage frame;
    uint8_t wire[128];
    size_t size;

    memset(&frame, 0, sizeof(frame));
    frame.channel = channel;
    frame.can_id = can_id;
    frame.payload_length = 1;
    frame.payload[0] = byte0;
    size = FrameMessage_Encode(&frame, wire, sizeof(wire));
    hub_events.on_frame(hub_events.context, wire, size);
}

static void feedHubError(uint16_t code, const char *detail)
{
    ErrorMessage error;
    uint8_t wire[128];
    size_t size;

    memset(&error, 0, sizeof(error));
    error.code = code;
    snprintf(error.detail, sizeof(error.detail), "%s", detail);
    size = ErrorMessage_Encode(&error, wire, sizeof(wire));
    hub_events.on_control(hub_events.context, wire, size, 1000);
}

static uint8_t sentMessageType(uint8_t slot)
{
    MessageHeader header;

    MessageHeader_Decode(&header, hub.control_log[slot], hub.control_sizes[slot]);

    return header.type;
}

static void decodeSentOpen(uint8_t slot, OpenMessage *open)
{
    MessageHeader header;

    MessageHeader_Decode(&header, hub.control_log[slot], hub.control_sizes[slot]);
    OpenMessage_Decode(open, hub.control_log[slot] + MESSAGE_HEADER_SIZE, header.length);
}

static void decodeSentList(uint8_t slot, ListMessage *list)
{
    MessageHeader header;

    MessageHeader_Decode(&header, hub.control_log[slot], hub.control_sizes[slot]);
    ListMessage_Decode(list, hub.control_log[slot] + MESSAGE_HEADER_SIZE, header.length);
}

static void decodeSentSubscribe(uint8_t slot, SubscribeMessage *subscribe)
{
    MessageHeader header;

    MessageHeader_Decode(&header, hub.control_log[slot], hub.control_sizes[slot]);
    SubscribeMessage_Decode(subscribe, hub.control_log[slot] + MESSAGE_HEADER_SIZE, header.length);
}

static void decodeSentFrame(FrameMessage *frame)
{
    MessageHeader header;

    MessageHeader_Decode(&header, hub.last_frame, hub.last_frame_size);
    FrameMessage_Decode(frame, hub.last_frame + MESSAGE_HEADER_SIZE, header.length);
}

static void onListReply(void *context, const ListReplyMessage *reply)
{
    (void)context;

    last_list_reply = *reply;
    list_reply_count++;
}

static void onOpenResult(void *context, uint8_t status, uint8_t channel, uint32_t interface_id)
{
    (void)context;

    last_open_status = status;
    last_open_channel = channel;
    last_open_interface_id = interface_id;
    open_result_count++;
}

static void onFrame(void *context, const FrameMessage *frame)
{
    (void)context;

    last_frame = *frame;
    frame_count++;
}

static void onError(void *context, uint16_t code, const char *detail)
{
    (void)context;

    last_error_code = code;
    snprintf(last_error_detail, sizeof(last_error_detail), "%s", detail);
    error_count++;
}

static void onDisconnected(void *context)
{
    (void)context;

    disconnect_count++;
}
