#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "client/txn_script/script_error.h"

#define SCRIPT_LINE_TOKENS_MAX 64

/*
 * Splits a transaction script into lines of tokens (doc/transaction-scripts.md).
 * Blank and comment-only lines are skipped; a line keeps its indentation in
 * spaces. A word is a NUMBER when it reads as decimal or 0x-hexadecimal, a
 * DURATION when digits are followed by us, ms or s, and a NAME otherwise, so
 * "F1" or "2B" stay names and the parser reads them as data bytes where bytes
 * are expected; every token keeps its text for that.
 */
typedef enum tscript_token_e {
    kSCRIPT_TOKEN_NAME = 0,
    kSCRIPT_TOKEN_NUMBER,
    kSCRIPT_TOKEN_DURATION,
    kSCRIPT_TOKEN_STRING,
    kSCRIPT_TOKEN_AMPERSAND,
    kSCRIPT_TOKEN_EQUALS,
    kSCRIPT_TOKEN_SHIFT_RIGHT,
    kSCRIPT_TOKEN_PLUS,
    kSCRIPT_TOKEN_RANGE,
    kSCRIPT_TOKEN_COMMA,
    kSCRIPT_TOKEN_OPEN_PARENTHESIS,
    kSCRIPT_TOKEN_CLOSE_PARENTHESIS,
    kSCRIPT_TOKEN_OPEN_BRACKET,
    kSCRIPT_TOKEN_CLOSE_BRACKET,
    kSCRIPT_TOKEN_ASSIGN,
    kSCRIPT_TOKEN_MAX,
} TSCRIPT_TOKEN;

typedef enum tscript_lex_result_e {
    kSCRIPT_LEX_LINE = 0,
    kSCRIPT_LEX_END,
    kSCRIPT_LEX_ERROR,
    kSCRIPT_LEX_RESULT_MAX,
} TSCRIPT_LEX_RESULT;

typedef struct {
    uint8_t kind;
    const char *text;
    uint16_t length;
    uint64_t number;
    uint8_t hex_digits;
} ScriptToken;

typedef struct {
    uint16_t line_number;
    uint16_t indent;
    uint8_t token_count;
    ScriptToken tokens[SCRIPT_LINE_TOKENS_MAX];
} ScriptLine;

typedef struct {
    const char *text;
    size_t size;
    size_t offset;
    uint16_t line_number;
} ScriptLexer;

void ScriptLexer_Init(ScriptLexer *self, const char *text, size_t size);
uint8_t ScriptLexer_NextLine(ScriptLexer *self, ScriptLine *line, ScriptError *error);
bool ScriptToken_Is(const ScriptToken *token, const char *word);
bool ScriptToken_HexByte(const ScriptToken *token, uint8_t *byte);
