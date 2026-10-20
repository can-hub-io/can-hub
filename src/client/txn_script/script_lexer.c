#include "client/txn_script/script_lexer.h"

#include <string.h>

#define COMMENT_MARK '#'
#define QUOTE '"'
#define DECIMAL_BASE 10
#define HEX_BASE 16
#define HEX_LETTER_VALUE 10
#define MICROSECONDS_PER_MILLISECOND 1000
#define MICROSECONDS_PER_SECOND 1000000
#define HEX_PREFIX_LENGTH 2
#define HEX_BYTE_DIGITS 2
#define BITS_PER_NIBBLE 4

typedef struct {
    char first;
    char second;
    uint8_t kind;
} Operator;

static const Operator OPERATORS[] = {
    { '=', '=', kSCRIPT_TOKEN_EQUALS },
    { '>', '>', kSCRIPT_TOKEN_SHIFT_RIGHT },
    { '.', '.', kSCRIPT_TOKEN_RANGE },
    { '&', '\0', kSCRIPT_TOKEN_AMPERSAND },
    { '+', '\0', kSCRIPT_TOKEN_PLUS },
    { ',', '\0', kSCRIPT_TOKEN_COMMA },
    { '(', '\0', kSCRIPT_TOKEN_OPEN_PARENTHESIS },
    { ')', '\0', kSCRIPT_TOKEN_CLOSE_PARENTHESIS },
    { '[', '\0', kSCRIPT_TOKEN_OPEN_BRACKET },
    { ']', '\0', kSCRIPT_TOKEN_CLOSE_BRACKET },
    { '=', '\0', kSCRIPT_TOKEN_ASSIGN },
};

static size_t lineEnd(const ScriptLexer *self);
static bool isWordCharacter(char character);
static bool isDecimalDigit(char character);
static bool isHexDigit(char character);
static uint8_t hexValue(char character);
static bool tokenizeLine(const char *text, size_t length, ScriptLine *line, ScriptError *error);
static size_t readWord(const char *text, size_t length, ScriptToken *token);
static size_t readString(const char *text, size_t length, ScriptToken *token);
static size_t readOperator(const char *text, size_t length, ScriptToken *token);
static void classifyWord(ScriptToken *token);
static bool readHexNumber(ScriptToken *token);
static bool readDecimalNumber(ScriptToken *token);
static bool readDuration(ScriptToken *token);

/* ---------- public ---------- */

void ScriptLexer_Init(ScriptLexer *self, const char *text, size_t size)
{
    self->text = text;
    self->size = size;
    self->offset = 0;
    self->line_number = 0;
}

uint8_t ScriptLexer_NextLine(ScriptLexer *self, ScriptLine *line, ScriptError *error)
{
    const char *start;
    size_t length;
    size_t indent;

    while (self->offset < self->size) {
        start = self->text + self->offset;
        length = lineEnd(self) - self->offset;
        self->offset += length < self->size - self->offset ? length + 1 : length;
        self->line_number++;

        indent = 0;
        while (indent < length && start[indent] == ' ') {
            indent++;
        }
        if (indent < length && start[indent] == '\t') {
            ScriptError_Set(error, self->line_number, "indent with spaces, not tabs");
            return kSCRIPT_LEX_ERROR;
        }

        memset(line, 0, sizeof(*line));
        line->line_number = self->line_number;
        line->indent = (uint16_t)indent;
        if (!tokenizeLine(start + indent, length - indent, line, error)) {
            error->line_number = self->line_number;
            return kSCRIPT_LEX_ERROR;
        }
        if (line->token_count > 0) {
            return kSCRIPT_LEX_LINE;
        }
    }

    return kSCRIPT_LEX_END;
}

bool ScriptToken_Is(const ScriptToken *token, const char *word)
{
    size_t length = strlen(word);

    return token->kind == kSCRIPT_TOKEN_NAME && token->length == length && memcmp(token->text, word, length) == 0;
}

bool ScriptToken_HexByte(const ScriptToken *token, uint8_t *byte)
{
    if (token->kind != kSCRIPT_TOKEN_NAME && token->kind != kSCRIPT_TOKEN_NUMBER) {
        return false;
    }
    if (token->length != HEX_BYTE_DIGITS || !isHexDigit(token->text[0]) || !isHexDigit(token->text[1])) {
        return false;
    }

    *byte = (uint8_t)((hexValue(token->text[0]) << BITS_PER_NIBBLE) | hexValue(token->text[1]));
    return true;
}

/* ---------- private ---------- */

static size_t lineEnd(const ScriptLexer *self)
{
    size_t end = self->offset;

    while (end < self->size && self->text[end] != '\n') {
        end++;
    }
    return end;
}

static bool isWordCharacter(char character)
{
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
           isDecimalDigit(character) || character == '_' || character == '-';
}

static bool isDecimalDigit(char character)
{
    return character >= '0' && character <= '9';
}

static bool isHexDigit(char character)
{
    return isDecimalDigit(character) || (character >= 'a' && character <= 'f') || (character >= 'A' && character <= 'F');
}

static uint8_t hexValue(char character)
{
    if (isDecimalDigit(character)) {
        return (uint8_t)(character - '0');
    }
    if (character >= 'a' && character <= 'f') {
        return (uint8_t)(character - 'a' + HEX_LETTER_VALUE);
    }
    return (uint8_t)(character - 'A' + HEX_LETTER_VALUE);
}

static bool tokenizeLine(const char *text, size_t length, ScriptLine *line, ScriptError *error)
{
    ScriptToken *token;
    size_t offset = 0;
    size_t consumed;

    while (offset < length) {
        if (text[offset] == ' ' || text[offset] == '\r') {
            offset++;
            continue;
        }
        if (text[offset] == COMMENT_MARK) {
            return true;
        }
        if (line->token_count == SCRIPT_LINE_TOKENS_MAX) {
            ScriptError_Set(error, 0, "line too long");
            return false;
        }

        token = &line->tokens[line->token_count];
        if (isWordCharacter(text[offset])) {
            consumed = readWord(text + offset, length - offset, token);
        } else if (text[offset] == QUOTE) {
            consumed = readString(text + offset, length - offset, token);
        } else {
            consumed = readOperator(text + offset, length - offset, token);
        }
        if (consumed == 0) {
            ScriptError_Set(error, 0, text[offset] == QUOTE ? "string not closed" : "unexpected character");
            return false;
        }

        line->token_count++;
        offset += consumed;
    }

    return true;
}

static size_t readWord(const char *text, size_t length, ScriptToken *token)
{
    size_t size = 0;

    while (size < length && isWordCharacter(text[size])) {
        size++;
    }

    token->text = text;
    token->length = (uint16_t)size;
    classifyWord(token);

    return size;
}

static size_t readString(const char *text, size_t length, ScriptToken *token)
{
    size_t size = 1;

    while (size < length && text[size] != QUOTE) {
        size++;
    }
    if (size == length) {
        return 0;
    }

    token->kind = kSCRIPT_TOKEN_STRING;
    token->text = text + 1;
    token->length = (uint16_t)(size - 1);

    return size + 1;
}

static size_t readOperator(const char *text, size_t length, ScriptToken *token)
{
    size_t i;

    for(i=0; i<sizeof(OPERATORS) / sizeof(OPERATORS[0]); i++) {
        if (text[0] != OPERATORS[i].first) {
            continue;
        }
        if (OPERATORS[i].second != '\0' && (length < 2 || text[1] != OPERATORS[i].second)) {
            continue;
        }

        token->kind = OPERATORS[i].kind;
        token->text = text;
        token->length = OPERATORS[i].second != '\0' ? 2 : 1;
        return token->length;
    }

    return 0;
}

static void classifyWord(ScriptToken *token)
{
    token->kind = kSCRIPT_TOKEN_NAME;
    if (!readHexNumber(token) && !readDecimalNumber(token)) {
        readDuration(token);
    }
}

static bool readHexNumber(ScriptToken *token)
{
    uint64_t value = 0;
    uint16_t i;

    if (token->length <= HEX_PREFIX_LENGTH || token->text[0] != '0' || (token->text[1] != 'x' && token->text[1] != 'X')) {
        return false;
    }
    for(i=HEX_PREFIX_LENGTH; i<token->length; i++) {
        if (!isHexDigit(token->text[i])) {
            return false;
        }
        value = value * HEX_BASE + hexValue(token->text[i]);
    }

    token->kind = kSCRIPT_TOKEN_NUMBER;
    token->number = value;
    token->hex_digits = (uint8_t)(token->length - HEX_PREFIX_LENGTH);

    return true;
}

static bool readDecimalNumber(ScriptToken *token)
{
    uint64_t value = 0;
    uint16_t i;

    for(i=0; i<token->length; i++) {
        if (!isDecimalDigit(token->text[i])) {
            return false;
        }
        value = value * DECIMAL_BASE + (uint64_t)(token->text[i] - '0');
    }

    token->kind = kSCRIPT_TOKEN_NUMBER;
    token->number = value;

    return true;
}

static bool readDuration(ScriptToken *token)
{
    uint64_t value = 0;
    uint16_t digits = 0;
    uint16_t unit_length;
    const char *unit;

    while (digits < token->length && isDecimalDigit(token->text[digits])) {
        value = value * DECIMAL_BASE + (uint64_t)(token->text[digits] - '0');
        digits++;
    }
    if (digits == 0) {
        return false;
    }

    unit = token->text + digits;
    unit_length = (uint16_t)(token->length - digits);
    if (unit_length == 2 && memcmp(unit, "us", 2) == 0) {
        token->number = value;
    } else if (unit_length == 2 && memcmp(unit, "ms", 2) == 0) {
        token->number = value * MICROSECONDS_PER_MILLISECOND;
    } else if (unit_length == 1 && unit[0] == 's') {
        token->number = value * MICROSECONDS_PER_SECOND;
    } else {
        return false;
    }

    token->kind = kSCRIPT_TOKEN_DURATION;
    return true;
}
