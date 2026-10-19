#include <cest>

#include <cstring>

extern "C" {
#include "client/frame_spec.h"
}

static FrameMessage frame;
static bool parsed;

static void parse(const char *text);

describe("frame_spec", []() {
    it("parses a classic frame", []() {
        parse("123#DEADBEEF");

        expect(parsed).toBe(true);
        expect(frame.can_id).toBe((uint32_t)0x123);
        expect(frame.payload_length).toBe((uint8_t)4);
        expect(frame.payload[3]).toBe((uint8_t)0xEF);
        expect(frame.frame_flags).toBe((uint8_t)0);
    });

    it("marks an id with more than three digits as extended", []() {
        parse("00000123#00");

        expect(parsed).toBe(true);
        expect(frame.can_id).toBe((uint32_t)(0x123 | FRAME_CAN_ID_FLAG_EFF));
    });

    it("accepts dots between data bytes", []() {
        parse("123#11.22.33");

        expect(parsed).toBe(true);
        expect(frame.payload_length).toBe((uint8_t)3);
        expect(frame.payload[2]).toBe((uint8_t)0x33);
    });

    it("sends more than eight data bytes as CAN FD", []() {
        parse("123#000102030405060708");

        expect(parsed).toBe(true);
        expect(frame.payload_length).toBe((uint8_t)9);
        expect(frame.frame_flags).toBe((uint8_t)FRAME_FLAG_FD);
    });

    it("parses a CAN FD frame with bit rate switch", []() {
        parse("123##1DEADBEEF");

        expect(parsed).toBe(true);
        expect(frame.payload_length).toBe((uint8_t)4);
        expect(frame.frame_flags).toBe((uint8_t)(FRAME_FLAG_FD | FRAME_FLAG_BRS));
    });

    it("parses a CAN FD frame without bit rate switch", []() {
        parse("123##0AA");

        expect(parsed).toBe(true);
        expect(frame.payload_length).toBe((uint8_t)1);
        expect(frame.frame_flags).toBe((uint8_t)FRAME_FLAG_FD);
    });

    it("parses a remote request", []() {
        parse("123#R");

        expect(parsed).toBe(true);
        expect(frame.can_id).toBe((uint32_t)(0x123 | FRAME_CAN_ID_FLAG_RTR));
        expect(frame.payload_length).toBe((uint8_t)0);
    });

    it("parses a remote request with a length", []() {
        parse("123#R3");

        expect(parsed).toBe(true);
        expect(frame.can_id).toBe((uint32_t)(0x123 | FRAME_CAN_ID_FLAG_RTR));
        expect(frame.payload_length).toBe((uint8_t)3);
    });

    it("rejects a CAN FD frame without flags", []() {
        parse("123##");

        expect(parsed).toBe(false);
    });

    it("rejects a remote request longer than eight bytes", []() {
        parse("123#R9");

        expect(parsed).toBe(false);
    });

    it("rejects an odd number of data digits", []() {
        parse("123#ABC");

        expect(parsed).toBe(false);
    });

    it("rejects a frame without a separator", []() {
        parse("123DEADBEEF");

        expect(parsed).toBe(false);
    });

    it("rejects an id beyond 29 bits", []() {
        parse("20000000#00");

        expect(parsed).toBe(false);
    });

    it("rejects more data than a CAN FD frame carries", []() {
        char text[160] = "123##0";

        memset(text + strlen(text), 'A', (FRAME_PAYLOAD_MAX_FD + 1) * 2);

        parse(text);

        expect(parsed).toBe(false);
    });
});

static void parse(const char *text)
{
    parsed = FrameSpec_Parse(text, &frame);
}
