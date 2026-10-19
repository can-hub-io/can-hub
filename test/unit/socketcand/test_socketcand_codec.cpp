#include <cest>

#include <cstring>

extern "C" {
#include "socketcand/domain/socketcand_codec.h"
}

static bool parse(const char *text, SocketcandCommand *command)
{
    return SocketcandCodec_Parse(text, strlen(text), command);
}

describe("socketcand_codec", []() {
    it("parses open with a namespaced bus", []() {
        SocketcandCommand command;
        bool ok = parse("open truck42/can0", &command);

        expect(ok).toBe(true);
        expect(command.kind).toBe((uint8_t)kSOCKETCAND_COMMAND_OPEN);
        expect(strcmp(command.bus, "truck42/can0") == 0).toBe(true);
    });

    it("parses rawmode and echo", []() {
        SocketcandCommand raw;
        SocketcandCommand echo;

        expect(parse("rawmode", &raw)).toBe(true);
        expect(raw.kind).toBe((uint8_t)kSOCKETCAND_COMMAND_RAWMODE);
        expect(parse("echo", &echo)).toBe(true);
        expect(echo.kind).toBe((uint8_t)kSOCKETCAND_COMMAND_ECHO);
    });

    it("parses a standard send frame", []() {
        SocketcandCommand command;
        bool ok = parse("send 123 2 de ad", &command);

        expect(ok).toBe(true);
        expect(command.kind).toBe((uint8_t)kSOCKETCAND_COMMAND_SEND);
        expect(command.frame.can_id).toBe((uint32_t)0x123);
        expect(command.frame.payload_length).toBe(2);
        expect(command.frame.payload[0]).toBe(0xDE);
        expect(command.frame.payload[1]).toBe(0xAD);
    });

    it("flags extended ids by width", []() {
        SocketcandCommand command;
        bool ok = parse("send 1FFFFFFF 0", &command);

        expect(ok).toBe(true);
        expect((command.frame.can_id & FRAME_CAN_ID_FLAG_EFF) != 0).toBe(true);
        expect(command.frame.can_id & FRAME_CAN_ID_MASK).toBe((uint32_t)0x1FFFFFFF);
        expect(command.frame.payload_length).toBe(0);
    });

    it("rejects a send whose byte count disagrees with the dlc", []() {
        SocketcandCommand command;

        expect(parse("send 123 3 de ad", &command)).toBe(false);
    });

    it("rejects unknown verbs", []() {
        SocketcandCommand command;

        expect(parse("bogus 1 2", &command)).toBe(false);
        expect(command.kind).toBe((uint8_t)kSOCKETCAND_COMMAND_UNKNOWN);
    });

    it("parses a bus bitrate and sample point", []() {
        SocketcandCommand command;
        bool ok = parse("truck42/can0 B 500000 875 0 0 0 0 0 0", &command);

        expect(ok).toBe(true);
        expect(command.kind).toBe((uint8_t)kSOCKETCAND_COMMAND_BITTIMING);
        expect(strcmp(command.bus, "truck42/can0") == 0).toBe(true);
        expect(command.settings.fields).toBe((uint32_t)(INTERFACE_FIELD_BITRATE | INTERFACE_FIELD_SAMPLE_POINT));
        expect(command.settings.bitrate).toBe((uint32_t)500000);
        expect(command.settings.sample_point).toBe((uint16_t)875);
    });

    it("parses a raw bus bit timing", []() {
        SocketcandCommand command;
        bool ok = parse("truck42/can0 B 0 0 125 6 7 2 1 0", &command);

        expect(ok).toBe(true);
        expect(command.settings.fields).toBe((uint32_t)INTERFACE_FIELD_BIT_TIMING);
        expect(command.settings.bit_timing.tq).toBe((uint32_t)125);
        expect(command.settings.bit_timing.prop_seg).toBe((uint32_t)6);
        expect(command.settings.bit_timing.phase_seg1).toBe((uint32_t)7);
        expect(command.settings.bit_timing.phase_seg2).toBe((uint32_t)2);
        expect(command.settings.bit_timing.sjw).toBe((uint32_t)1);
    });

    it("rejects a bus bit timing with missing values", []() {
        SocketcandCommand command;

        expect(parse("truck42/can0 B 500000 875", &command)).toBe(false);
    });

    it("parses bus control modes", []() {
        SocketcandCommand command;
        bool ok = parse("truck42/can0 C 1 0 1", &command);

        expect(ok).toBe(true);
        expect(command.kind).toBe((uint8_t)kSOCKETCAND_COMMAND_CONTROLMODE);
        expect(command.settings.fields).toBe((uint32_t)INTERFACE_FIELD_CTRLMODE);
        expect(command.settings.ctrlmode_mask).toBe((uint32_t)(INTERFACE_CTRLMODE_LISTEN_ONLY | INTERFACE_CTRLMODE_LOOPBACK | INTERFACE_CTRLMODE_TRIPLE_SAMPLING));
        expect(command.settings.ctrlmode_flags).toBe((uint32_t)(INTERFACE_CTRLMODE_LISTEN_ONLY | INTERFACE_CTRLMODE_TRIPLE_SAMPLING));
    });

    it("rejects a control mode other than 0 or 1", []() {
        SocketcandCommand command;

        expect(parse("truck42/can0 C 2 0 0", &command)).toBe(false);
    });

    it("rejects an unknown bus command", []() {
        SocketcandCommand command;

        expect(parse("truck42/can0 Z 1", &command)).toBe(false);
    });

    it("renders the greeting", []() {
        char out[64];

        SocketcandCodec_RenderGreeting(out, sizeof(out));

        expect(strcmp(out, "< hi >") == 0).toBe(true);
    });

    it("renders an error with its text", []() {
        char out[64];

        SocketcandCodec_RenderError(out, sizeof(out), "no bus open");

        expect(strcmp(out, "< error no bus open >") == 0).toBe(true);
    });

    it("renders an error with its reason and detail", []() {
        char out[96];

        SocketcandCodec_RenderReasonedError(out, sizeof(out), "apply failed", "bitrate not supported");

        expect(strcmp(out, "< error apply failed: bitrate not supported >") == 0).toBe(true);
    });

    it("renders an error with only its reason when there is no detail", []() {
        char out[96];

        SocketcandCodec_RenderReasonedError(out, sizeof(out), "unknown interface", "");

        expect(strcmp(out, "< error unknown interface >") == 0).toBe(true);
    });

    it("renders a received frame with padded microseconds", []() {
        FrameMessage frame;
        char out[128];

        memset(&frame, 0, sizeof(frame));
        frame.can_id = 0x123;
        frame.timestamp_us = (uint64_t)23 * 1000000 + 424242;
        frame.payload_length = 4;
        frame.payload[0] = 0x1A;
        frame.payload[1] = 0x22;
        frame.payload[2] = 0x03;
        frame.payload[3] = 0x44;

        SocketcandCodec_RenderFrame(out, sizeof(out), &frame);

        expect(strcmp(out, "< frame 123 23.424242 1A220344 >") == 0).toBe(true);
    });

    it("renders an empty extended frame with an empty data field", []() {
        FrameMessage frame;
        char out[128];

        memset(&frame, 0, sizeof(frame));
        frame.can_id = 0x1ABCDEF | FRAME_CAN_ID_FLAG_EFF;
        frame.timestamp_us = (uint64_t)5 * 1000000 + 7;
        frame.payload_length = 0;

        SocketcandCodec_RenderFrame(out, sizeof(out), &frame);

        expect(strcmp(out, "< frame 1ABCDEF 5.000007  >") == 0).toBe(true);
    });
});
