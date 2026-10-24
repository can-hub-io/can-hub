#include <cest>
#include <cstring>

extern "C" {
#include "platform/linux/libcanhub/canhub_frames.h"
}

#define STANDARD_ID 0x123
#define EXTENDED_ID 0x18DAF110
#define EFF_FLAG 0x80000000u
#define RTR_FLAG 0x40000000u
#define ERR_FLAG 0x20000000u
#define TIMESTAMP_US 1234567
#define FD_LENGTH 12

static CanHubFrame classicFrame(void);

describe("canhub_frames", []() {
    describe("to wire", []() {
        it("carries a standard id as is and an extended one with its flag", []() {
            CanHubFrame standard = classicFrame();
            CanHubFrame extended = classicFrame();
            FrameMessage standard_message;
            FrameMessage extended_message;

            extended.id = CANHUB_EXTENDED_ID(EXTENDED_ID);

            CanHubFrames_ToWire(&standard, &standard_message);
            CanHubFrames_ToWire(&extended, &extended_message);

            expect(standard_message.can_id).toBe((uint32_t)STANDARD_ID);
            expect(extended_message.can_id).toBe((uint32_t)(EXTENDED_ID | EFF_FLAG));
            expect(standard_message.payload_length).toBe(2);
            expect(standard_message.payload[1]).toBe(0xFE);
        });

        it("turns the remote request flag into the id's RTR bit", []() {
            CanHubFrame frame = classicFrame();
            FrameMessage message;

            frame.flags = CANHUB_FRAME_FLAG_RTR;

            CanHubFrames_ToWire(&frame, &message);

            expect(message.can_id).toBe((uint32_t)(STANDARD_ID | RTR_FLAG));
            expect(message.frame_flags).toBe(0);
        });

        it("refuses a standard id over 11 bits", []() {
            CanHubFrame frame = classicFrame();
            FrameMessage message;

            frame.id.value = 0x800;

            expect(CanHubFrames_ToWire(&frame, &message)).toBe(false);
        });

        it("refuses more than 8 bytes without CAN FD", []() {
            CanHubFrame frame = classicFrame();
            FrameMessage message;
            bool classic;
            bool fd;

            frame.length = FD_LENGTH;
            classic = CanHubFrames_ToWire(&frame, &message);
            frame.flags = CANHUB_FRAME_FLAG_FD;
            fd = CanHubFrames_ToWire(&frame, &message);

            expect(classic).toBe(false);
            expect(fd).toBe(true);
        });
    });

    describe("from wire", []() {
        it("splits the wire id into value and extended, and the flag bits into frame flags", []() {
            FrameMessage message;
            CanHubFrame frame;

            memset(&message, 0, sizeof(message));
            message.can_id = EXTENDED_ID | EFF_FLAG | ERR_FLAG;
            message.timestamp_us = TIMESTAMP_US;
            message.frame_flags = FRAME_FLAG_FD;
            message.route_flags = FRAME_ROUTE_FLAG_BRIDGED;
            message.payload_length = 1;
            message.payload[0] = 0xAB;

            CanHubFrames_FromWire(&message, &frame);

            expect(frame.id.value).toBe((uint32_t)EXTENDED_ID);
            expect(frame.id.extended).toBe(true);
            expect(frame.flags).toBe((uint8_t)(CANHUB_FRAME_FLAG_FD | CANHUB_FRAME_FLAG_ERROR | CANHUB_FRAME_FLAG_BRIDGED));
            expect(frame.timestamp_us).toBe((uint64_t)TIMESTAMP_US);
            expect(frame.payload[0]).toBe(0xAB);
        });
    });

    describe("filters", []() {
        it("compares every id bit and the extended flag, except the ignored bits", []() {
            CanHubFilter filter = { CANHUB_STANDARD_ID(0x700), 0x00F };
            CanFilter wire;
            bool converted;

            converted = CanHubFrames_FilterToWire(&filter, &wire);

            expect(converted).toBe(true);
            expect(wire.can_id).toBe((uint32_t)0x700);
            expect(wire.can_mask).toBe((uint32_t)(0x7F0 | EFF_FLAG));
        });

        it("filters an extended id with its flag", []() {
            CanHubFilter filter = { CANHUB_EXTENDED_ID(EXTENDED_ID), 0 };
            CanFilter wire;

            CanHubFrames_FilterToWire(&filter, &wire);

            expect(wire.can_id).toBe((uint32_t)(EXTENDED_ID | EFF_FLAG));
            expect(wire.can_mask).toBe((uint32_t)(0x1FFFFFFF | EFF_FLAG));
        });

        it("refuses ignored bits over the id width", []() {
            CanHubFilter filter = { CANHUB_STANDARD_ID(0x700), 0x800 };
            CanFilter wire;

            expect(CanHubFrames_FilterToWire(&filter, &wire)).toBe(false);
        });
    });
});

static CanHubFrame classicFrame(void)
{
    CanHubFrame frame;

    memset(&frame, 0, sizeof(frame));
    frame.id = CANHUB_STANDARD_ID(STANDARD_ID);
    frame.length = 2;
    frame.payload[0] = 0xCA;
    frame.payload[1] = 0xFE;
    return frame;
}
