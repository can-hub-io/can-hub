#include "socketcand/domain/socketcand_codec.h"

#include <string.h>

#define SOCKETCAND_DLC_MAX 64
#define HEX_DIGITS_U32_MAX 8
#define DEC_DIGITS_U64_MAX 20
#define USEC_DIGITS 6
#define MICROSECONDS_PER_SECOND 1000000u

static bool parseSend(const char *text, size_t length, size_t *cursor, FrameMessage *frame);
static size_t nextToken(const char *text, size_t length, size_t *cursor, const char **token);
static bool tokenEquals(const char *token, size_t token_length, const char *literal);
static int32_t hexNibble(char character);
static bool parseHex(const char *token, size_t token_length, uint32_t *value);
static bool parseDecimal(const char *token, size_t token_length, uint32_t *value);
static size_t formatHexUpper(uint32_t value, char *out);
static size_t formatDecimal(uint64_t value, char *out);
static size_t formatDecimalPadded(uint32_t value, char *out, size_t width);
static bool append(char *out, size_t out_size, size_t *position, const char *text, size_t length);

/* ---------- public ---------- */

bool SocketcandCodec_Parse(const char *text, size_t length, SocketcandCommand *command)
{
    const char *token;
    size_t token_length;
    size_t cursor = 0;
    size_t bus_length;

    memset(command, 0, sizeof(*command));
    command->kind = kSOCKETCAND_COMMAND_UNKNOWN;

    token_length = nextToken(text, length, &cursor, &token);
    if (token_length == 0) {
        return false;
    }

    if (tokenEquals(token, token_length, "open")) {
        token_length = nextToken(text, length, &cursor, &token);
        if (token_length == 0 || token_length >= SOCKETCAND_BUS_NAME_SIZE) {
            return false;
        }
        bus_length = token_length;
        memcpy(command->bus, token, bus_length);
        command->bus[bus_length] = '\0';
        command->kind = kSOCKETCAND_COMMAND_OPEN;
        return true;
    }
    if (tokenEquals(token, token_length, "rawmode")) {
        command->kind = kSOCKETCAND_COMMAND_RAWMODE;
        return true;
    }
    if (tokenEquals(token, token_length, "bcmmode")) {
        command->kind = kSOCKETCAND_COMMAND_BCMMODE;
        return true;
    }
    if (tokenEquals(token, token_length, "echo")) {
        command->kind = kSOCKETCAND_COMMAND_ECHO;
        return true;
    }
    if (tokenEquals(token, token_length, "send")) {
        if (!parseSend(text, length, &cursor, &command->frame)) {
            return false;
        }
        command->kind = kSOCKETCAND_COMMAND_SEND;
        return true;
    }

    return false;
}

size_t SocketcandCodec_RenderGreeting(char *out, size_t out_size)
{
    size_t position = 0;

    if (!append(out, out_size, &position, "< hi >", 6)) {
        return 0;
    }
    out[position] = '\0';

    return position;
}

size_t SocketcandCodec_RenderOk(char *out, size_t out_size)
{
    size_t position = 0;

    if (!append(out, out_size, &position, "< ok >", 6)) {
        return 0;
    }
    out[position] = '\0';

    return position;
}

size_t SocketcandCodec_RenderEcho(char *out, size_t out_size)
{
    size_t position = 0;

    if (!append(out, out_size, &position, "< echo >", 8)) {
        return 0;
    }
    out[position] = '\0';

    return position;
}

size_t SocketcandCodec_RenderError(char *out, size_t out_size, const char *detail)
{
    size_t position = 0;

    if (!append(out, out_size, &position, "< error ", 8)) {
        return 0;
    }
    if (!append(out, out_size, &position, detail, strlen(detail))) {
        return 0;
    }
    if (!append(out, out_size, &position, " >", 2)) {
        return 0;
    }
    out[position] = '\0';

    return position;
}

size_t SocketcandCodec_RenderFrame(char *out, size_t out_size, const FrameMessage *frame)
{
    char number[DEC_DIGITS_U64_MAX + 1];
    size_t position = 0;
    size_t number_length;
    uint64_t seconds = frame->timestamp_us / MICROSECONDS_PER_SECOND;
    uint32_t microseconds = (uint32_t)(frame->timestamp_us % MICROSECONDS_PER_SECOND);
    uint8_t i;

    if (!append(out, out_size, &position, "< frame ", 8)) {
        return 0;
    }
    number_length = formatHexUpper(frame->can_id & FRAME_CAN_ID_MASK, number);
    if (!append(out, out_size, &position, number, number_length)) {
        return 0;
    }
    if (!append(out, out_size, &position, " ", 1)) {
        return 0;
    }
    number_length = formatDecimal(seconds, number);
    if (!append(out, out_size, &position, number, number_length)) {
        return 0;
    }
    if (!append(out, out_size, &position, ".", 1)) {
        return 0;
    }
    number_length = formatDecimalPadded(microseconds, number, USEC_DIGITS);
    if (!append(out, out_size, &position, number, number_length)) {
        return 0;
    }
    if (!append(out, out_size, &position, " ", 1)) {
        return 0;
    }
    for(i=0; i<frame->payload_length; i++) {
        char byte_text[2];
        byte_text[0] = "0123456789ABCDEF"[(frame->payload[i] >> 4) & 0x0F];
        byte_text[1] = "0123456789ABCDEF"[frame->payload[i] & 0x0F];
        if (!append(out, out_size, &position, byte_text, 2)) {
            return 0;
        }
    }
    if (!append(out, out_size, &position, " >", 2)) {
        return 0;
    }
    out[position] = '\0';

    return position;
}

/* ---------- private ---------- */

static bool parseSend(const char *text, size_t length, size_t *cursor, FrameMessage *frame)
{
    const char *token;
    size_t token_length;
    uint32_t can_id;
    uint32_t dlc;
    uint32_t byte_value;
    uint8_t i;

    memset(frame, 0, sizeof(*frame));

    token_length = nextToken(text, length, cursor, &token);
    if (token_length == 0 || token_length > HEX_DIGITS_U32_MAX || !parseHex(token, token_length, &can_id)) {
        return false;
    }
    frame->can_id = can_id & FRAME_CAN_ID_MASK;
    if (token_length > 3 || can_id > FRAME_CAN_ID_SFF_MAX) {
        frame->can_id |= FRAME_CAN_ID_FLAG_EFF;
    }

    token_length = nextToken(text, length, cursor, &token);
    if (token_length == 0 || !parseDecimal(token, token_length, &dlc) || dlc > SOCKETCAND_DLC_MAX) {
        return false;
    }
    frame->payload_length = (uint8_t)dlc;
    if (dlc > FRAME_PAYLOAD_MAX_CLASSIC) {
        frame->frame_flags |= FRAME_FLAG_FD;
    }

    for(i=0; i<dlc; i++) {
        token_length = nextToken(text, length, cursor, &token);
        if (token_length == 0 || token_length > 2 || !parseHex(token, token_length, &byte_value) || byte_value > 0xFF) {
            return false;
        }
        frame->payload[i] = (uint8_t)byte_value;
    }

    token_length = nextToken(text, length, cursor, &token);
    if (token_length != 0) {
        return false;
    }

    return true;
}

static size_t nextToken(const char *text, size_t length, size_t *cursor, const char **token)
{
    size_t start;
    size_t end;

    while (*cursor < length && (text[*cursor] == ' ' || text[*cursor] == '\t')) {
        (*cursor)++;
    }
    start = *cursor;
    while (*cursor < length && text[*cursor] != ' ' && text[*cursor] != '\t') {
        (*cursor)++;
    }
    end = *cursor;

    *token = text + start;

    return end - start;
}

static bool tokenEquals(const char *token, size_t token_length, const char *literal)
{
    size_t literal_length = strlen(literal);

    return token_length == literal_length && memcmp(token, literal, literal_length) == 0;
}

static int32_t hexNibble(char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }

    return -1;
}

static bool parseHex(const char *token, size_t token_length, uint32_t *value)
{
    uint32_t accumulator = 0;
    int32_t nibble;
    size_t i;

    if (token_length == 0) {
        return false;
    }
    for(i=0; i<token_length; i++) {
        nibble = hexNibble(token[i]);
        if (nibble < 0) {
            return false;
        }
        accumulator = (accumulator << 4) | (uint32_t)nibble;
    }
    *value = accumulator;

    return true;
}

static bool parseDecimal(const char *token, size_t token_length, uint32_t *value)
{
    uint32_t accumulator = 0;
    size_t i;

    if (token_length == 0) {
        return false;
    }
    for(i=0; i<token_length; i++) {
        if (token[i] < '0' || token[i] > '9') {
            return false;
        }
        accumulator = accumulator * 10 + (uint32_t)(token[i] - '0');
    }
    *value = accumulator;

    return true;
}

static size_t formatHexUpper(uint32_t value, char *out)
{
    char reversed[HEX_DIGITS_U32_MAX];
    size_t digits = 0;
    size_t i;

    if (value == 0) {
        out[0] = '0';
        return 1;
    }
    while (value > 0) {
        reversed[digits++] = "0123456789ABCDEF"[value & 0x0F];
        value >>= 4;
    }
    for(i=0; i<digits; i++) {
        out[i] = reversed[digits - 1 - i];
    }

    return digits;
}

static size_t formatDecimal(uint64_t value, char *out)
{
    char reversed[DEC_DIGITS_U64_MAX];
    size_t digits = 0;
    size_t i;

    if (value == 0) {
        out[0] = '0';
        return 1;
    }
    while (value > 0) {
        reversed[digits++] = (char)('0' + (value % 10));
        value /= 10;
    }
    for(i=0; i<digits; i++) {
        out[i] = reversed[digits - 1 - i];
    }

    return digits;
}

static size_t formatDecimalPadded(uint32_t value, char *out, size_t width)
{
    size_t i;

    for(i=0; i<width; i++) {
        out[width - 1 - i] = (char)('0' + (value % 10));
        value /= 10;
    }

    return width;
}

static bool append(char *out, size_t out_size, size_t *position, const char *text, size_t length)
{
    if (*position + length >= out_size) {
        return false;
    }
    memcpy(out + *position, text, length);
    *position += length;

    return true;
}
