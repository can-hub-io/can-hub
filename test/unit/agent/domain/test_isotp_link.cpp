#include <cest>
#include <cstring>
#include <initializer_list>

extern "C" {
#include "agent/domain/isotp_link.h"
}

#define REQUEST_ID 0x7E0
#define RESPONSE_ID 0x7E8
#define OTHER_ID 0x7E9
#define CHANNEL 3
#define PADDING 0xCC
#define TRANSMITTED_MAX 128
#define MESSAGE_SIZE_MAX 256
#define START_US 1000
#define ST_MIN_10_MS 0x0A
#define ST_MIN_500_US 0xF5
#define LONG_MESSAGE_SIZE 120
#define SIXTEENTH_FRAME 16

static IsotpLink isotp;
static FrameMessage transmitted[TRANSMITTED_MAX];
static uint32_t transmit_count;
static bool transmit_accepts;
static uint8_t message[MESSAGE_SIZE_MAX];
static uint8_t events[TRANSMITTED_MAX];
static FrameMessage answer;

static void resetLink(void);
static void openLink(uint8_t flags, uint8_t block_size, uint8_t st_min);
static void sendMessage(uint16_t length, uint64_t now_us);
static uint8_t fromPeer(std::initializer_list<uint8_t> bytes, uint64_t now_us);
static uint8_t fromId(uint32_t can_id, std::initializer_list<uint8_t> bytes, uint64_t now_us);
static uint8_t receiveFirstFrameOf20(void);
static bool payloadIs(const FrameMessage *frame, std::initializer_list<uint8_t> bytes);
static bool transmitFrame(void *context, const FrameMessage *frame);

describe("isotp_link", []() {
    describe("sending", []() {
        beforeEach([]() {
            resetLink();
        });

        it("sends a short message as one padded single frame", []() {
            sendMessage(3, START_US);
            events[0] = IsotpLink_Tick(&isotp, START_US);

            expect(events[0]).toBe((uint8_t)kISOTP_EVENT_SENT);
            expect(transmit_count).toBe((uint32_t)1);
            expect(transmitted[0].can_id).toBe((uint32_t)REQUEST_ID);
            expect(payloadIs(&transmitted[0], { 0x03, 0x00, 0x01, 0x02, PADDING, PADDING, PADDING, PADDING })).toBe(true);
        });

        it("sends a single frame as short as its data without padding", []() {
            openLink(0, 0, 0);

            sendMessage(3, START_US);
            IsotpLink_Tick(&isotp, START_US);

            expect(payloadIs(&transmitted[0], { 0x03, 0x00, 0x01, 0x02 })).toBe(true);
        });

        it("sends a first frame and waits for the peer's flow control", []() {
            sendMessage(20, START_US);
            events[0] = IsotpLink_Tick(&isotp, START_US);
            events[1] = IsotpLink_Tick(&isotp, START_US + 100);

            expect(events[0]).toBe((uint8_t)kISOTP_EVENT_NONE);
            expect(events[1]).toBe((uint8_t)kISOTP_EVENT_NONE);
            expect(transmit_count).toBe((uint32_t)1);
            expect(payloadIs(&transmitted[0], { 0x10, 0x14, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05 })).toBe(true);
        });

        it("sends the consecutive frames once the flow control allows them", []() {
            sendMessage(20, START_US);
            IsotpLink_Tick(&isotp, START_US);

            fromPeer({ 0x30, 0x00, 0x00 }, START_US + 100);
            events[0] = IsotpLink_Tick(&isotp, START_US + 100);

            expect(events[0]).toBe((uint8_t)kISOTP_EVENT_SENT);
            expect(transmit_count).toBe((uint32_t)3);
            expect(payloadIs(&transmitted[1], { 0x21, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C })).toBe(true);
            expect(payloadIs(&transmitted[2], { 0x22, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13 })).toBe(true);
        });

        it("keeps the peer's minimum separation time between consecutive frames", []() {
            uint32_t sent_at_flow_control;
            uint32_t sent_before_separation;
            uint32_t sent_after_separation;

            sendMessage(20, START_US);
            IsotpLink_Tick(&isotp, START_US);
            fromPeer({ 0x30, 0x00, ST_MIN_10_MS }, START_US);

            IsotpLink_Tick(&isotp, START_US);
            sent_at_flow_control = transmit_count;
            IsotpLink_Tick(&isotp, START_US + 5000);
            sent_before_separation = transmit_count;
            IsotpLink_Tick(&isotp, START_US + 10000);
            sent_after_separation = transmit_count;

            expect(sent_at_flow_control).toBe((uint32_t)2);
            expect(sent_before_separation).toBe((uint32_t)2);
            expect(sent_after_separation).toBe((uint32_t)3);
        });

        it("reads separation times in hundreds of microseconds", []() {
            sendMessage(20, START_US);
            IsotpLink_Tick(&isotp, START_US);
            fromPeer({ 0x30, 0x00, ST_MIN_500_US }, START_US);

            IsotpLink_Tick(&isotp, START_US);

            expect(IsotpLink_NextWakeUs(&isotp)).toBe((uint64_t)(START_US + 500));
        });

        it("waits for the next flow control after a block", []() {
            uint32_t sent_in_first_block;

            sendMessage(20, START_US);
            IsotpLink_Tick(&isotp, START_US);
            fromPeer({ 0x30, 0x01, 0x00 }, START_US);

            IsotpLink_Tick(&isotp, START_US);
            sent_in_first_block = transmit_count;
            fromPeer({ 0x30, 0x01, 0x00 }, START_US + 100);
            events[0] = IsotpLink_Tick(&isotp, START_US + 100);

            expect(sent_in_first_block).toBe((uint32_t)2);
            expect(events[0]).toBe((uint8_t)kISOTP_EVENT_SENT);
            expect(transmit_count).toBe((uint32_t)3);
        });

        it("wraps the sequence number after 15", []() {
            sendMessage(LONG_MESSAGE_SIZE, START_US);
            IsotpLink_Tick(&isotp, START_US);
            fromPeer({ 0x30, 0x00, 0x00 }, START_US);

            IsotpLink_Tick(&isotp, START_US);

            expect(transmitted[15].payload[0]).toBe(0x2F);
            expect(transmitted[SIXTEENTH_FRAME].payload[0]).toBe(0x20);
        });

        it("retries a frame the bus refuses", []() {
            transmit_accepts = false;
            sendMessage(3, START_US);
            events[0] = IsotpLink_Tick(&isotp, START_US);

            transmit_accepts = true;
            events[1] = IsotpLink_Tick(&isotp, START_US + ISOTP_RETRY_US);

            expect(events[0]).toBe((uint8_t)kISOTP_EVENT_NONE);
            expect(events[1]).toBe((uint8_t)kISOTP_EVENT_SENT);
        });

        it("fails when the peer answers overflow", []() {
            sendMessage(20, START_US);
            IsotpLink_Tick(&isotp, START_US);

            events[0] = fromPeer({ 0x32, 0x00, 0x00 }, START_US + 100);

            expect(events[0]).toBe((uint8_t)kISOTP_EVENT_FAILED);
            expect(IsotpLink_FailureReason(&isotp)).toBe((uint8_t)kTXN_REASON_OVERFLOW);
        });

        it("fails after too many flow control waits in a row", []() {
            uint8_t i;

            sendMessage(20, START_US);
            IsotpLink_Tick(&isotp, START_US);

            for(i=0; i<=ISOTP_WAIT_FRAMES_MAX; i++) {
                events[i] = fromPeer({ 0x31, 0x00, 0x00 }, START_US + i);
            }

            expect(events[ISOTP_WAIT_FRAMES_MAX - 1]).toBe((uint8_t)kISOTP_EVENT_NONE);
            expect(events[ISOTP_WAIT_FRAMES_MAX]).toBe((uint8_t)kISOTP_EVENT_FAILED);
            expect(IsotpLink_FailureReason(&isotp)).toBe((uint8_t)kTXN_REASON_WAIT_LIMIT);
        });

        it("fails when the flow control never comes", []() {
            sendMessage(20, START_US);
            IsotpLink_Tick(&isotp, START_US);

            events[0] = IsotpLink_Tick(&isotp, START_US + ISOTP_TIMEOUT_US);

            expect(events[0]).toBe((uint8_t)kISOTP_EVENT_FAILED);
            expect(IsotpLink_FailureReason(&isotp)).toBe((uint8_t)kTXN_REASON_FLOW_CONTROL_TIMEOUT);
        });
    });

    describe("receiving", []() {
        beforeEach([]() {
            resetLink();
        });

        it("hands over a single frame answer", []() {
            events[0] = fromPeer({ 0x03, 0x7F, 0x36, 0x78, PADDING, PADDING, PADDING, PADDING }, START_US);
            IsotpLink_MessageFrame(&isotp, &answer);

            expect(events[0]).toBe((uint8_t)kISOTP_EVENT_MESSAGE);
            expect(answer.can_id).toBe((uint32_t)RESPONSE_ID);
            expect(payloadIs(&answer, { 0x7F, 0x36, 0x78 })).toBe(true);
        });

        it("answers a first frame with flow control and reassembles the rest", []() {
            events[0] = receiveFirstFrameOf20();
            IsotpLink_Tick(&isotp, START_US);
            fromPeer({ 0x21, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C }, START_US + 100);
            events[1] = fromPeer({ 0x22, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13 }, START_US + 200);
            IsotpLink_MessageFrame(&isotp, &answer);

            expect(events[0]).toBe((uint8_t)kISOTP_EVENT_NONE);
            expect(events[1]).toBe((uint8_t)kISOTP_EVENT_MESSAGE);
            expect(transmitted[0].can_id).toBe((uint32_t)REQUEST_ID);
            expect(payloadIs(&transmitted[0], { 0x30, 0x00, 0x00, PADDING, PADDING, PADDING, PADDING, PADDING })).toBe(true);
            expect(answer.payload_length).toBe(20);
            expect(memcmp(answer.payload, message, 20)).toBe(0);
        });

        it("asks for the next block after its own block size", []() {
            openLink(TXN_ISOTP_FLAG_PADDING, 1, ST_MIN_10_MS);

            receiveFirstFrameOf20();
            IsotpLink_Tick(&isotp, START_US);
            fromPeer({ 0x21, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C }, START_US + 100);
            IsotpLink_Tick(&isotp, START_US + 100);

            expect(transmit_count).toBe((uint32_t)2);
            expect(transmitted[1].payload[1]).toBe(1);
            expect(transmitted[1].payload[2]).toBe(ST_MIN_10_MS);
        });

        it("fails on a consecutive frame out of order", []() {
            receiveFirstFrameOf20();
            IsotpLink_Tick(&isotp, START_US);

            events[0] = fromPeer({ 0x22, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13 }, START_US + 100);

            expect(events[0]).toBe((uint8_t)kISOTP_EVENT_FAILED);
            expect(IsotpLink_FailureReason(&isotp)).toBe((uint8_t)kTXN_REASON_SEQUENCE_ERROR);
        });

        it("fails when the next consecutive frame never comes", []() {
            receiveFirstFrameOf20();
            IsotpLink_Tick(&isotp, START_US);

            events[0] = IsotpLink_Tick(&isotp, START_US + ISOTP_TIMEOUT_US);

            expect(events[0]).toBe((uint8_t)kISOTP_EVENT_FAILED);
            expect(IsotpLink_FailureReason(&isotp)).toBe((uint8_t)kTXN_REASON_CONSECUTIVE_FRAME_TIMEOUT);
        });

        it("refuses an answer longer than it can hold with flow control overflow", []() {
            events[0] = fromPeer({ 0x10, 0x00, 0x00, 0x00, 0x20, 0x00, 0x01, 0x02 }, START_US);

            expect(events[0]).toBe((uint8_t)kISOTP_EVENT_FAILED);
            expect(IsotpLink_FailureReason(&isotp)).toBe((uint8_t)kTXN_REASON_OVERFLOW);
            expect(transmitted[0].payload[0]).toBe(0x32);
        });

        it("ignores frames on other ids and the echo of its own", []() {
            FrameMessage echo;

            events[0] = fromId(OTHER_ID, { 0x03, 0x7F, 0x36, 0x78 }, START_US);
            memset(&echo, 0, sizeof(echo));
            echo.can_id = RESPONSE_ID;
            echo.payload_length = 1;
            echo.payload[0] = 0x01;
            echo.route_flags = FRAME_ROUTE_FLAG_ECHO;
            events[1] = IsotpLink_OnFrame(&isotp, &echo, START_US);

            expect(events[0]).toBe((uint8_t)kISOTP_EVENT_NONE);
            expect(events[1]).toBe((uint8_t)kISOTP_EVENT_NONE);
        });
    });
});

static void resetLink(void)
{
    uint16_t i;

    transmit_count = 0;
    transmit_accepts = true;
    memset(events, 0, sizeof(events));
    for(i=0; i<MESSAGE_SIZE_MAX; i++) {
        message[i] = (uint8_t)i;
    }
    openLink(TXN_ISOTP_FLAG_PADDING, 0, 0);
}

static void openLink(uint8_t flags, uint8_t block_size, uint8_t st_min)
{
    TxnIsotpParameters parameters;
    IsotpLinkPort port = { NULL, transmitFrame };

    memset(&parameters, 0, sizeof(parameters));
    parameters.response_id = RESPONSE_ID;
    parameters.flags = flags;
    parameters.padding = PADDING;
    parameters.tx_data_length = FRAME_PAYLOAD_MAX_CLASSIC;
    parameters.block_size = block_size;
    parameters.st_min = st_min;
    IsotpLink_Init(&isotp, port);
    IsotpLink_Open(&isotp, REQUEST_ID, CHANNEL, &parameters);
}

static void sendMessage(uint16_t length, uint64_t now_us)
{
    IsotpLink_Send(&isotp, message, length, now_us);
}

static uint8_t fromPeer(std::initializer_list<uint8_t> bytes, uint64_t now_us)
{
    return fromId(RESPONSE_ID, bytes, now_us);
}

static uint8_t fromId(uint32_t can_id, std::initializer_list<uint8_t> bytes, uint64_t now_us)
{
    FrameMessage frame;

    memset(&frame, 0, sizeof(frame));
    frame.can_id = can_id;
    frame.payload_length = (uint8_t)bytes.size();
    memcpy(frame.payload, bytes.begin(), bytes.size());
    return IsotpLink_OnFrame(&isotp, &frame, now_us);
}

static uint8_t receiveFirstFrameOf20(void)
{
    return fromPeer({ 0x10, 0x14, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05 }, START_US);
}

static bool payloadIs(const FrameMessage *frame, std::initializer_list<uint8_t> bytes)
{
    if (frame->payload_length != bytes.size()) {
        return false;
    }
    return memcmp(frame->payload, bytes.begin(), bytes.size()) == 0;
}

static bool transmitFrame(void *context, const FrameMessage *frame)
{
    (void)context;
    if (!transmit_accepts || transmit_count == TRANSMITTED_MAX) {
        return false;
    }
    transmitted[transmit_count++] = *frame;
    return true;
}
