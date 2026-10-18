#include "client/frame_spec.h"

#include <string.h>

#define FRAME_SPEC_SEPARATOR '#'
#define FRAME_SPEC_REMOTE 'R'
#define FRAME_SPEC_BYTE_SEPARATOR '.'
#define FRAME_SPEC_SFF_DIGITS_MAX 3
#define FRAME_SPEC_ID_DIGITS_MAX 8
#define FRAME_SPEC_FD_FLAG_BRS 0x01
#define HEX_DIGIT_BITS 4
#define HEX_PAIR_DIGITS 2
#define HEX_LETTER_VALUE 10

static bool parseCanId(const char *text, const char **end, FrameMessage *frame);
static bool parseFdFrame(const char *text, FrameMessage *frame);
static bool parseRemoteFrame(const char *text, FrameMessage *frame);
static bool parseClassicFrame(const char *text, FrameMessage *frame);
static bool parseBytes(const char *text, FrameMessage *frame);
static bool hexDigit(char character, uint8_t *value);

/* ---------- public ---------- */

bool FrameSpec_Parse(const char *text, FrameMessage *frame)
{
    const char *cursor;

    memset(frame, 0, sizeof(*frame));
    if (!parseCanId(text, &cursor, frame) || *cursor != FRAME_SPEC_SEPARATOR) {
        return false;
    }

    cursor++;
    if (*cursor == FRAME_SPEC_SEPARATOR) {
        return parseFdFrame(cursor + 1, frame);
    }
    if (*cursor == FRAME_SPEC_REMOTE) {
        return parseRemoteFrame(cursor + 1, frame);
    }

    return parseClassicFrame(cursor, frame);
}

/* ---------- private ---------- */

static bool parseCanId(const char *text, const char **end, FrameMessage *frame)
{
    uint32_t can_id = 0;
    uint8_t digits = 0;
    uint8_t nibble;

    while (hexDigit(text[digits], &nibble)) {
        if (digits == FRAME_SPEC_ID_DIGITS_MAX) {
            return false;
        }
        can_id = (can_id << HEX_DIGIT_BITS) | nibble;
        digits++;
    }
    if (digits == 0 || can_id > FRAME_CAN_ID_MASK) {
        return false;
    }

    frame->can_id = can_id;
    if (digits > FRAME_SPEC_SFF_DIGITS_MAX || can_id > FRAME_CAN_ID_SFF_MAX) {
        frame->can_id |= FRAME_CAN_ID_FLAG_EFF;
    }
    *end = text + digits;

    return true;
}

static bool parseFdFrame(const char *text, FrameMessage *frame)
{
    uint8_t flags;

    if (!hexDigit(text[0], &flags)) {
        return false;
    }

    frame->frame_flags = FRAME_FLAG_FD;
    if (flags & FRAME_SPEC_FD_FLAG_BRS) {
        frame->frame_flags |= FRAME_FLAG_BRS;
    }

    return parseBytes(text + 1, frame);
}

static bool parseRemoteFrame(const char *text, FrameMessage *frame)
{
    frame->can_id |= FRAME_CAN_ID_FLAG_RTR;
    if (text[0] == '\0') {
        return true;
    }
    if (text[0] < '0' || text[0] > '0' + FRAME_PAYLOAD_MAX_CLASSIC || text[1] != '\0') {
        return false;
    }

    frame->payload_length = (uint8_t)(text[0] - '0');

    return true;
}

static bool parseClassicFrame(const char *text, FrameMessage *frame)
{
    if (!parseBytes(text, frame)) {
        return false;
    }
    if (frame->payload_length > FRAME_PAYLOAD_MAX_CLASSIC) {
        frame->frame_flags |= FRAME_FLAG_FD;
    }

    return true;
}

static bool parseBytes(const char *text, FrameMessage *frame)
{
    uint8_t high;
    uint8_t low;

    while (*text != '\0') {
        if (*text == FRAME_SPEC_BYTE_SEPARATOR) {
            text++;
            continue;
        }
        if (!hexDigit(text[0], &high) || !hexDigit(text[1], &low)) {
            return false;
        }
        if (frame->payload_length == FRAME_PAYLOAD_MAX_FD) {
            return false;
        }
        frame->payload[frame->payload_length++] = (uint8_t)((high << HEX_DIGIT_BITS) | low);
        text += HEX_PAIR_DIGITS;
    }

    return true;
}

static bool hexDigit(char character, uint8_t *value)
{
    if (character >= '0' && character <= '9') {
        *value = (uint8_t)(character - '0');
        return true;
    }
    if (character >= 'a' && character <= 'f') {
        *value = (uint8_t)(character - 'a' + HEX_LETTER_VALUE);
        return true;
    }
    if (character >= 'A' && character <= 'F') {
        *value = (uint8_t)(character - 'A' + HEX_LETTER_VALUE);
        return true;
    }

    return false;
}
