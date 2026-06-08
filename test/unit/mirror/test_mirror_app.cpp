#include <cest>

#include <cstring>

extern "C" {
#include "can_port_mock.h"
#include "mirror/mirror_app.h"
#include "protocol/frame_message.h"
#include "protocol/message_header.h"
#include "protocol/open_message.h"
#include "transport_port_mock.h"
}

#define TEST_INTERFACE_ID 7

static MirrorApp mirror;
static TransportPortMock hub;
static CanPortMock can;
static TransportEvents hub_events;

static void connect();
static void feedOpenAck(uint8_t status, uint8_t channel);
static void feedHubFrame(uint8_t channel, uint32_t can_id, uint8_t byte0, uint8_t byte1);
static void feedHubError();
static void sendLocalFrame(uint32_t can_id, uint8_t byte0);
static void decodeSentOpen(uint8_t slot, OpenMessage *open);
static void decodeSentFrame(FrameMessage *frame);

describe("mirror_app", []() {
    beforeEach([]() {
        TransportPortMock_Reset(&hub);
        CanPortMock_Reset(&can);
        MirrorApp_Init(&mirror, &hub.port, &can.port, TEST_INTERFACE_ID);
        hub_events = MirrorApp_TransportEvents(&mirror);
    });

    it("sends HELLO then OPEN with want-write on connect", []() {
        OpenMessage open;

        connect();
        decodeSentOpen(1, &open);

        expect(hub.control_count).toBe(2);
        expect(open.interface_id).toBe((uint32_t)TEST_INTERFACE_ID);
        expect((open.flags & OPEN_FLAG_WANT_WRITE) != 0).toBe(true);
    });

    it("enters pumping with write granted on OPEN_ACK ok", []() {
        connect();
        feedOpenAck(OPEN_STATUS_OK, 5);

        expect(MirrorApp_State(&mirror)).toBe((uint8_t)kMIRROR_PUMPING);
        expect(MirrorApp_CanWrite(&mirror)).toBe(true);
    });

    it("writes hub frames to the local can port while pumping", []() {
        connect();
        feedOpenAck(OPEN_STATUS_OK, 5);
        feedHubFrame(5, 0x123, 0xDE, 0xAD);

        expect(can.write_count).toBe(1);
        expect(can.last_interface_index).toBe((uint8_t)0);
        expect(can.last_frame.can_id).toBe((uint32_t)0x123);
        expect(can.last_frame.payload[0]).toBe((uint8_t)0xDE);
        expect(can.last_frame.payload[1]).toBe((uint8_t)0xAD);
    });

    it("forwards local can frames to the hub on the open channel", []() {
        FrameMessage frame;

        connect();
        feedOpenAck(OPEN_STATUS_OK, 5);
        sendLocalFrame(0x321, 0xBE);
        decodeSentFrame(&frame);

        expect(hub.frame_count).toBe(1);
        expect(frame.channel).toBe((uint8_t)5);
        expect(frame.can_id).toBe((uint32_t)0x321);
    });

    it("drops local can frames before the channel is open", []() {
        connect();
        sendLocalFrame(0x321, 0xBE);

        expect(hub.frame_count).toBe(0);
    });

    it("downgrades to read-only when write is denied", []() {
        OpenMessage open;

        connect();
        feedOpenAck(OPEN_STATUS_WRITE_DENIED, 0);
        decodeSentOpen(2, &open);
        feedOpenAck(OPEN_STATUS_OK, 6);
        sendLocalFrame(0x321, 0xBE);

        expect(hub.control_count).toBe(3);
        expect((open.flags & OPEN_FLAG_WANT_WRITE) != 0).toBe(false);
        expect(MirrorApp_State(&mirror)).toBe((uint8_t)kMIRROR_PUMPING);
        expect(MirrorApp_CanWrite(&mirror)).toBe(false);
        expect(hub.frame_count).toBe(0);
    });

    it("fails on a rejected open", []() {
        connect();
        feedOpenAck(OPEN_STATUS_REJECTED, 0);

        expect(MirrorApp_State(&mirror)).toBe((uint8_t)kMIRROR_FAILED);
    });

    it("fails on disconnect", []() {
        connect();
        feedOpenAck(OPEN_STATUS_OK, 5);
        hub_events.on_disconnected(hub_events.context, 2000);

        expect(MirrorApp_State(&mirror)).toBe((uint8_t)kMIRROR_FAILED);
    });

    it("fails on a hub error message", []() {
        connect();
        feedHubError();

        expect(MirrorApp_State(&mirror)).toBe((uint8_t)kMIRROR_FAILED);
    });
});

static void connect()
{
    hub_events.on_connected(hub_events.context);
}

static void feedOpenAck(uint8_t status, uint8_t channel)
{
    OpenAckMessage ack = { status, channel, TEST_INTERFACE_ID };
    uint8_t wire[64];
    size_t size = OpenAckMessage_Encode(&ack, wire, sizeof(wire));

    hub_events.on_control(hub_events.context, wire, size, 1000);
}

static void feedHubFrame(uint8_t channel, uint32_t can_id, uint8_t byte0, uint8_t byte1)
{
    FrameMessage frame;
    uint8_t wire[128];
    size_t size;

    memset(&frame, 0, sizeof(frame));
    frame.channel = channel;
    frame.can_id = can_id;
    frame.payload_length = 2;
    frame.payload[0] = byte0;
    frame.payload[1] = byte1;
    size = FrameMessage_Encode(&frame, wire, sizeof(wire));
    hub_events.on_frame(hub_events.context, wire, size);
}

static void feedHubError()
{
    MessageHeader header = { kMESSAGE_TYPE_ERROR, 0, 0 };
    uint8_t wire[8];
    size_t size = MessageHeader_Encode(&header, wire, sizeof(wire));

    hub_events.on_control(hub_events.context, wire, size, 1000);
}

static void sendLocalFrame(uint32_t can_id, uint8_t byte0)
{
    FrameMessage frame;

    memset(&frame, 0, sizeof(frame));
    frame.can_id = can_id;
    frame.payload_length = 1;
    frame.payload[0] = byte0;
    MirrorApp_OnCanFrame(&mirror, &frame);
}

static void decodeSentOpen(uint8_t slot, OpenMessage *open)
{
    MessageHeader header;

    MessageHeader_Decode(&header, hub.control_log[slot], hub.control_sizes[slot]);
    OpenMessage_Decode(open, hub.control_log[slot] + MESSAGE_HEADER_SIZE, header.length);
}

static void decodeSentFrame(FrameMessage *frame)
{
    MessageHeader header;

    MessageHeader_Decode(&header, hub.last_frame, hub.last_frame_size);
    FrameMessage_Decode(frame, hub.last_frame + MESSAGE_HEADER_SIZE, header.length);
}
