#include "client/txn_script/script_parser.h"

#include <string.h>

#include "client/txn_script/script_lexer.h"
#include "shared/bounded_string.h"

#define BLOCK_DEPTH_MAX 8
#define BYTE_VALUE_MAX 0xFF
#define NAME_LENGTH_MAX 0xFF
#define CRC_PARAMETERS_GIVEN ((1u << kCRC_PARAMETER_MAX) - 1)

typedef enum tblock_e {
    kBLOCK_ROOT = 0,
    kBLOCK_TEMPLATE,
    kBLOCK_ROSTER,
    kBLOCK_FOR,
    kBLOCK_MAX,
} TBLOCK;

typedef struct {
    uint8_t kind;
    uint16_t index;
    uint16_t header_indent;
    uint16_t indent;
    bool awaiting_first_line;
    uint16_t last_child;
    uint16_t header_line;
} Block;

typedef struct {
    const ScriptLine *line;
    uint8_t position;
} Cursor;

typedef enum tcrc_parameter_e {
    kCRC_PARAMETER_WIDTH = 0,
    kCRC_PARAMETER_POLYNOMIAL,
    kCRC_PARAMETER_INITIAL,
    kCRC_PARAMETER_FINAL_XOR,
    kCRC_PARAMETER_REFLECT_INPUT,
    kCRC_PARAMETER_REFLECT_OUTPUT,
    kCRC_PARAMETER_MAX,
} TCRC_PARAMETER;

typedef struct {
    const char *name;
    uint8_t bit;
    uint32_t *target;
} NumberParameter;

typedef struct {
    const char *name;
    uint8_t bit;
    bool *target;
} SwitchParameter;

typedef struct {
    Script *script;
    Block blocks[BLOCK_DEPTH_MAX];
    uint8_t depth;
    ScriptError *error;
} Parser;

static bool placeLine(Parser *self, const ScriptLine *line);
static bool closeBlock(Parser *self);
static bool openBlock(Parser *self, uint8_t kind, uint16_t index, const ScriptLine *line);
static Block *currentBlock(Parser *self);
static bool parseLine(Parser *self, const ScriptLine *line);
static bool parseRootOrForLine(Parser *self, Cursor *cursor);
static bool parseTemplateLine(Parser *self, Cursor *cursor);
static bool parseRosterLine(Parser *self, Cursor *cursor);
static bool parseConstant(Parser *self, Cursor *cursor);
static bool parseConstantValue(Parser *self, Cursor *cursor, ScriptConstant *constant);
static bool parseCrcModel(Parser *self, Cursor *cursor, ScriptConstant *constant);
static bool parseCrcParameter(Parser *self, Cursor *cursor, ChecksumModel *model, uint8_t *given);
static bool parseTemplateHeader(Parser *self, Cursor *cursor);
static bool parseRosterHeader(Parser *self, Cursor *cursor);
static bool parseTimeout(Parser *self, Cursor *cursor, ScriptTemplate *template_node);
static bool parseSend(Parser *self, Cursor *cursor);
static bool parseExpect(Parser *self, Cursor *cursor);
static bool parseExpectClause(Parser *self, Cursor *cursor, ScriptStatement *statement);
static bool parseFor(Parser *self, Cursor *cursor);
static bool parseChunks(Parser *self, Cursor *cursor, ScriptStatement *statement);
static bool parseDataItems(Parser *self, Cursor *cursor, ScriptStatement *statement);
static bool parseDataItem(Parser *self, const ScriptToken *token, ScriptDataItem *item);
static bool parseListReference(Parser *self, Cursor *cursor, ScriptListReference *reference);
static bool parseList(Parser *self, Cursor *cursor, ScriptList *list);
static bool parseCondition(Parser *self, Cursor *cursor, uint16_t *condition_index);
static bool parseTerm(Parser *self, Cursor *cursor, ScriptTerm *term);
static bool parseField(Parser *self, Cursor *cursor, ScriptTerm *term);
static bool parseShiftedField(Parser *self, Cursor *cursor, ScriptTerm *term);
static bool parseByteIndex(Parser *self, Cursor *cursor, ScriptTerm *term);
static bool parseValue(Parser *self, Cursor *cursor, ScriptValue *value);
static bool parseCheck(Parser *self, Cursor *cursor, ScriptCheck *check);
static uint16_t addStatement(Parser *self, uint8_t kind, uint16_t line_number);
static void linkStatement(Parser *self, uint16_t index);
static bool fail(Parser *self, const char *message);
static const ScriptToken *peek(const Cursor *cursor);
static const ScriptToken *next(Cursor *cursor);
static bool atEnd(const Cursor *cursor);
static bool acceptWord(Cursor *cursor, const char *word);
static bool acceptKind(Cursor *cursor, uint8_t kind);
static bool expectWord(Parser *self, Cursor *cursor, const char *word, const char *message);
static bool expectKind(Parser *self, Cursor *cursor, uint8_t kind, const char *message);
static bool expectName(Parser *self, Cursor *cursor, ScriptName *name, const char *message);
static bool expectNumber(Parser *self, Cursor *cursor, ScriptNumber *number, const char *message);
static bool expectEnd(Parser *self, const Cursor *cursor);

/* ---------- public ---------- */

bool ScriptParser_Parse(Script *script, const char *text, size_t size, ScriptError *error)
{
    Parser parser;
    ScriptLexer lexer;
    ScriptLine line;
    uint8_t result;

    memset(script, 0, sizeof(*script));
    script->first_statement = SCRIPT_NO_INDEX;
    memset(&parser, 0, sizeof(parser));
    parser.script = script;
    parser.error = error;
    parser.depth = 1;
    parser.blocks[0].kind = kBLOCK_ROOT;
    parser.blocks[0].last_child = SCRIPT_NO_INDEX;
    ScriptLexer_Init(&lexer, text, size);

    while ((result = ScriptLexer_NextLine(&lexer, &line, error)) == kSCRIPT_LEX_LINE) {
        if (!placeLine(&parser, &line) || !parseLine(&parser, &line)) {
            return false;
        }
    }
    if (result == kSCRIPT_LEX_ERROR) {
        return false;
    }
    while (parser.depth > 1) {
        if (!closeBlock(&parser)) {
            return false;
        }
    }

    return true;
}

/* ---------- private ---------- */

static bool placeLine(Parser *self, const ScriptLine *line)
{
    Block *block;

    ScriptError_Set(self->error, line->line_number, "");
    while (self->depth > 1 && !currentBlock(self)->awaiting_first_line && line->indent < currentBlock(self)->indent) {
        if (!closeBlock(self)) {
            return false;
        }
    }

    block = currentBlock(self);
    if (block->awaiting_first_line) {
        if (line->indent <= block->header_indent) {
            ScriptError_Set(self->error, block->header_line, "expected an indented block after this line");
            return false;
        }
        block->indent = line->indent;
        block->awaiting_first_line = false;
        return true;
    }
    if (line->indent != block->indent) {
        return fail(self, "indentation does not match any open block");
    }

    return true;
}

static bool closeBlock(Parser *self)
{
    Block *block = currentBlock(self);
    ScriptTemplate *template_node;

    if (block->awaiting_first_line) {
        ScriptError_Set(self->error, block->header_line, "expected an indented block after this line");
        return false;
    }
    if (block->kind == kBLOCK_ROSTER) {
        template_node = &self->script->templates[block->index];
        if (template_node->success == SCRIPT_NO_INDEX) {
            ScriptError_Set(self->error, block->header_line, "a template for needs a success line");
            return false;
        }
    }

    self->depth--;
    return true;
}

static bool openBlock(Parser *self, uint8_t kind, uint16_t index, const ScriptLine *line)
{
    Block *block;

    if (self->depth == BLOCK_DEPTH_MAX) {
        return fail(self, "blocks nested too deep");
    }

    block = &self->blocks[self->depth++];
    memset(block, 0, sizeof(*block));
    block->kind = kind;
    block->index = index;
    block->header_indent = line->indent;
    block->header_line = line->line_number;
    block->awaiting_first_line = true;
    block->last_child = SCRIPT_NO_INDEX;

    return true;
}

static Block *currentBlock(Parser *self)
{
    return &self->blocks[self->depth - 1];
}

static bool parseLine(Parser *self, const ScriptLine *line)
{
    Cursor cursor = { line, 0 };
    uint8_t kind = currentBlock(self)->kind;

    if (kind == kBLOCK_TEMPLATE) {
        return parseTemplateLine(self, &cursor);
    }
    if (kind == kBLOCK_ROSTER) {
        return parseRosterLine(self, &cursor);
    }
    return parseRootOrForLine(self, &cursor);
}

static bool parseRootOrForLine(Parser *self, Cursor *cursor)
{
    bool at_root = currentBlock(self)->kind == kBLOCK_ROOT;

    if (acceptWord(cursor, "send")) {
        return parseSend(self, cursor);
    }
    if (acceptWord(cursor, "expect")) {
        return parseExpect(self, cursor);
    }
    if (acceptWord(cursor, "for")) {
        return parseFor(self, cursor);
    }
    if (!at_root) {
        return fail(self, "a for block holds send, expect and for lines");
    }
    if (acceptWord(cursor, "exclusive")) {
        self->script->exclusive = true;
        return expectEnd(self, cursor);
    }
    if (acceptWord(cursor, "template")) {
        return parseTemplateHeader(self, cursor);
    }
    if (cursor->line->token_count > 1 && cursor->line->tokens[1].kind == kSCRIPT_TOKEN_ASSIGN) {
        return parseConstant(self, cursor);
    }

    return fail(self, "unknown statement");
}

static bool parseTemplateLine(Parser *self, Cursor *cursor)
{
    ScriptTemplate *template_node = &self->script->templates[currentBlock(self)->index];

    if (acceptWord(cursor, "success")) {
        if (template_node->has_roster) {
            return fail(self, "this template already waits for its for block");
        }
        return parseCondition(self, cursor, &template_node->success) && expectEnd(self, cursor);
    }
    if (acceptWord(cursor, "failure")) {
        return parseCondition(self, cursor, &template_node->failure) && expectEnd(self, cursor);
    }
    if (acceptWord(cursor, "pending")) {
        return parseCondition(self, cursor, &template_node->pending) && expectEnd(self, cursor);
    }
    if (acceptWord(cursor, "timeout")) {
        return parseTimeout(self, cursor, template_node);
    }
    if (acceptWord(cursor, "on")) {
        if (!expectWord(self, cursor, "mismatch", "expected on mismatch abort") ||
            !expectWord(self, cursor, "abort", "expected on mismatch abort")) {
            return false;
        }
        template_node->mismatch_abort = true;
        return expectEnd(self, cursor);
    }
    if (acceptWord(cursor, "for")) {
        return parseRosterHeader(self, cursor);
    }

    return fail(self, "a template holds success, failure, pending, for, on mismatch abort and timeout lines");
}

static bool parseRosterLine(Parser *self, Cursor *cursor)
{
    ScriptTemplate *template_node = &self->script->templates[currentBlock(self)->index];

    if (!acceptWord(cursor, "success")) {
        return fail(self, "a template for holds one success line");
    }
    if (template_node->success != SCRIPT_NO_INDEX) {
        return fail(self, "a template for holds one success line");
    }

    return parseCondition(self, cursor, &template_node->success) && expectEnd(self, cursor);
}

static bool parseConstant(Parser *self, Cursor *cursor)
{
    ScriptConstant *constant;

    if (self->script->constant_count == SCRIPT_CONSTANTS_MAX) {
        return fail(self, "too many constants");
    }

    constant = &self->script->constants[self->script->constant_count];
    memset(constant, 0, sizeof(*constant));
    constant->line_number = cursor->line->line_number;
    if (!expectName(self, cursor, &constant->name, "expected a constant name") ||
        !expectKind(self, cursor, kSCRIPT_TOKEN_ASSIGN, "expected =") ||
        !parseConstantValue(self, cursor, constant) ||
        !expectEnd(self, cursor)) {
        return false;
    }

    self->script->constant_count++;
    return true;
}

static bool parseConstantValue(Parser *self, Cursor *cursor, ScriptConstant *constant)
{
    const ScriptToken *path;

    if (acceptWord(cursor, "read")) {
        constant->kind = kSCRIPT_CONSTANT_FILE;
        if (!expectKind(self, cursor, kSCRIPT_TOKEN_OPEN_PARENTHESIS, "expected read(\"file\")")) {
            return false;
        }
        path = next(cursor);
        if (path == NULL || path->kind != kSCRIPT_TOKEN_STRING || path->length >= SCRIPT_PATH_SIZE) {
            return fail(self, "expected read(\"file\")");
        }
        memcpy(constant->path, path->text, path->length);
        return expectKind(self, cursor, kSCRIPT_TOKEN_CLOSE_PARENTHESIS, "expected read(\"file\")");
    }
    if (acceptWord(cursor, "crc")) {
        constant->kind = kSCRIPT_CONSTANT_CRC;
        return parseCrcModel(self, cursor, constant);
    }
    if (cursor->line->token_count == cursor->position + 1) {
        constant->kind = kSCRIPT_CONSTANT_NUMBER;
        return expectNumber(self, cursor, &constant->number, "expected a number, a list, read() or crc()");
    }

    constant->kind = kSCRIPT_CONSTANT_LIST;
    return parseList(self, cursor, &constant->list);
}

static bool parseCrcModel(Parser *self, Cursor *cursor, ScriptConstant *constant)
{
    uint8_t given = 0;

    constant->model.kind = kCHECKSUM_KIND_CRC;
    if (!expectKind(self, cursor, kSCRIPT_TOKEN_OPEN_PARENTHESIS, "expected crc(...)")) {
        return false;
    }
    do {
        if (!parseCrcParameter(self, cursor, &constant->model, &given)) {
            return false;
        }
    } while (acceptKind(cursor, kSCRIPT_TOKEN_COMMA));
    if (!expectKind(self, cursor, kSCRIPT_TOKEN_CLOSE_PARENTHESIS, "expected ) after the crc parameters")) {
        return false;
    }
    if (given != CRC_PARAMETERS_GIVEN) {
        return fail(self, "crc needs width, polynomial, initial, reflect_input, reflect_output and final_xor");
    }

    return true;
}

static bool parseCrcParameter(Parser *self, Cursor *cursor, ChecksumModel *model, uint8_t *given)
{
    const NumberParameter numbers[] = {
        { "width", kCRC_PARAMETER_WIDTH, NULL },
        { "polynomial", kCRC_PARAMETER_POLYNOMIAL, &model->polynomial },
        { "initial", kCRC_PARAMETER_INITIAL, &model->initial },
        { "final_xor", kCRC_PARAMETER_FINAL_XOR, &model->final_xor },
    };
    const SwitchParameter switches[] = {
        { "reflect_input", kCRC_PARAMETER_REFLECT_INPUT, &model->reflect_input },
        { "reflect_output", kCRC_PARAMETER_REFLECT_OUTPUT, &model->reflect_output },
    };
    ScriptNumber number;
    uint8_t i;

    for(i=0; i<sizeof(numbers) / sizeof(numbers[0]); i++) {
        if (!acceptWord(cursor, numbers[i].name)) {
            continue;
        }
        *given |= (uint8_t)(1u << numbers[i].bit);
        if (!expectNumber(self, cursor, &number, "expected a number")) {
            return false;
        }
        if (numbers[i].target == NULL) {
            model->width = number.value > UINT8_MAX ? UINT8_MAX : (uint8_t)number.value;
        } else {
            *numbers[i].target = (uint32_t)number.value;
        }
        return true;
    }
    for(i=0; i<sizeof(switches) / sizeof(switches[0]); i++) {
        if (!acceptWord(cursor, switches[i].name)) {
            continue;
        }
        *given |= (uint8_t)(1u << switches[i].bit);
        *switches[i].target = acceptWord(cursor, "yes");
        return *switches[i].target || expectWord(self, cursor, "no", "expected yes or no");
    }

    return fail(self, "unknown crc parameter");
}

static bool parseTemplateHeader(Parser *self, Cursor *cursor)
{
    ScriptTemplate *template_node;
    uint16_t index = self->script->template_count;

    if (index == SCRIPT_TEMPLATES_MAX) {
        return fail(self, "too many templates");
    }

    template_node = &self->script->templates[index];
    memset(template_node, 0, sizeof(*template_node));
    template_node->line_number = cursor->line->line_number;
    template_node->success = SCRIPT_NO_INDEX;
    template_node->failure = SCRIPT_NO_INDEX;
    template_node->pending = SCRIPT_NO_INDEX;
    if (!expectName(self, cursor, &template_node->name, "expected a template name") || !expectEnd(self, cursor)) {
        return false;
    }

    self->script->template_count++;
    return openBlock(self, kBLOCK_TEMPLATE, index, cursor->line);
}

static bool parseRosterHeader(Parser *self, Cursor *cursor)
{
    uint16_t index = currentBlock(self)->index;
    ScriptTemplate *template_node = &self->script->templates[index];
    ScriptNumber quorum;

    if (template_node->has_roster || template_node->success != SCRIPT_NO_INDEX) {
        return fail(self, "a template waits on one success, either a line or a for block");
    }
    if (peek(cursor) != NULL && peek(cursor)->kind == kSCRIPT_TOKEN_NUMBER) {
        if (!expectNumber(self, cursor, &quorum, "expected a count") ||
            !expectWord(self, cursor, "of", "expected for N of variable in list")) {
            return false;
        }
        if (quorum.value == 0 || quorum.value > BYTE_VALUE_MAX) {
            return fail(self, "the count must be at least 1");
        }
        template_node->quorum = (uint8_t)quorum.value;
    }
    if (!expectName(self, cursor, &template_node->roster_variable, "expected a variable name") ||
        !expectWord(self, cursor, "in", "expected in") ||
        !parseListReference(self, cursor, &template_node->roster) ||
        !expectEnd(self, cursor)) {
        return false;
    }

    template_node->has_roster = true;
    return openBlock(self, kBLOCK_ROSTER, index, cursor->line);
}

static bool parseTimeout(Parser *self, Cursor *cursor, ScriptTemplate *template_node)
{
    const ScriptToken *duration = next(cursor);

    if (duration == NULL || duration->kind != kSCRIPT_TOKEN_DURATION) {
        return fail(self, "expected a time such as 50ms or 2s");
    }

    template_node->has_timeout = true;
    template_node->timeout_us = (uint32_t)duration->number;
    if (acceptWord(cursor, "then")) {
        if (!expectWord(self, cursor, "continue", "expected then continue")) {
            return false;
        }
        template_node->continue_on_timeout = true;
    }

    return expectEnd(self, cursor);
}

static bool parseSend(Parser *self, Cursor *cursor)
{
    uint16_t index = addStatement(self, kSCRIPT_STATEMENT_SEND, cursor->line->line_number);
    ScriptStatement *statement;
    const ScriptToken *duration;

    if (index == SCRIPT_NO_INDEX) {
        return false;
    }

    statement = &self->script->statements[index];
    if (!parseValue(self, cursor, &statement->id) ||
        !expectWord(self, cursor, "data", "expected data after the identifier") ||
        !parseDataItems(self, cursor, statement)) {
        return false;
    }
    if (acceptWord(cursor, "gap")) {
        duration = next(cursor);
        if (duration == NULL || duration->kind != kSCRIPT_TOKEN_DURATION) {
            return fail(self, "expected a time such as 1ms after gap");
        }
        statement->has_gap = true;
        statement->gap_us = (uint32_t)duration->number;
    }
    if (acceptWord(cursor, "expect") && !parseExpectClause(self, cursor, statement)) {
        return false;
    }

    linkStatement(self, index);
    return expectEnd(self, cursor);
}

static bool parseExpect(Parser *self, Cursor *cursor)
{
    uint16_t index = addStatement(self, kSCRIPT_STATEMENT_EXPECT, cursor->line->line_number);

    if (index == SCRIPT_NO_INDEX || !parseExpectClause(self, cursor, &self->script->statements[index])) {
        return false;
    }

    linkStatement(self, index);
    return expectEnd(self, cursor);
}

static bool parseExpectClause(Parser *self, Cursor *cursor, ScriptStatement *statement)
{
    statement->has_expect = true;
    if (!expectName(self, cursor, &statement->template_name, "expected a template name after expect")) {
        return false;
    }
    if (acceptWord(cursor, "where")) {
        return parseCondition(self, cursor, &statement->where);
    }

    return true;
}

static bool parseFor(Parser *self, Cursor *cursor)
{
    uint16_t index = addStatement(self, kSCRIPT_STATEMENT_FOR, cursor->line->line_number);
    ScriptStatement *statement;

    if (index == SCRIPT_NO_INDEX) {
        return false;
    }

    statement = &self->script->statements[index];
    if (!expectName(self, cursor, &statement->variable, "expected a variable name") ||
        !expectWord(self, cursor, "in", "expected in")) {
        return false;
    }
    if (acceptWord(cursor, "chunks")) {
        statement->iterable = kSCRIPT_ITERABLE_CHUNKS;
        if (!parseChunks(self, cursor, statement)) {
            return false;
        }
    } else {
        statement->iterable = kSCRIPT_ITERABLE_LIST;
        if (!parseListReference(self, cursor, &statement->list)) {
            return false;
        }
    }
    if (!expectEnd(self, cursor)) {
        return false;
    }

    linkStatement(self, index);
    return openBlock(self, kBLOCK_FOR, index, cursor->line);
}

static bool parseChunks(Parser *self, Cursor *cursor, ScriptStatement *statement)
{
    ScriptNumber number;

    if (!expectKind(self, cursor, kSCRIPT_TOKEN_OPEN_PARENTHESIS, "expected chunks(data, size)") ||
        !expectName(self, cursor, &statement->chunk_source, "expected the data to split") ||
        !expectKind(self, cursor, kSCRIPT_TOKEN_COMMA, "expected chunks(data, size)") ||
        !expectNumber(self, cursor, &number, "expected the chunk size")) {
        return false;
    }
    statement->chunk_size = (uint32_t)number.value;
    if (acceptKind(cursor, kSCRIPT_TOKEN_COMMA)) {
        if (!expectWord(self, cursor, "pad", "expected pad=0xFF") ||
            !expectKind(self, cursor, kSCRIPT_TOKEN_ASSIGN, "expected pad=0xFF") ||
            !expectNumber(self, cursor, &number, "expected pad=0xFF")) {
            return false;
        }
        if (number.value > BYTE_VALUE_MAX) {
            return fail(self, "the padding is one byte");
        }
        statement->has_pad = true;
        statement->pad = (uint8_t)number.value;
    }

    return expectKind(self, cursor, kSCRIPT_TOKEN_CLOSE_PARENTHESIS, "expected ) after chunks");
}

static bool parseDataItems(Parser *self, Cursor *cursor, ScriptStatement *statement)
{
    const ScriptToken *token;
    ScriptDataItem *item;

    statement->first_data_item = self->script->data_item_count;
    while ((token = peek(cursor)) != NULL && !ScriptToken_Is(token, "gap") && !ScriptToken_Is(token, "expect")) {
        if (self->script->data_item_count == SCRIPT_DATA_ITEMS_MAX) {
            return fail(self, "too many data bytes in the script");
        }
        item = &self->script->data_items[self->script->data_item_count];
        if (!parseDataItem(self, token, item)) {
            return false;
        }
        next(cursor);
        self->script->data_item_count++;
        statement->data_count++;
    }
    if (statement->data_count == 0) {
        return fail(self, "expected the data bytes");
    }

    return true;
}

static bool parseDataItem(Parser *self, const ScriptToken *token, ScriptDataItem *item)
{
    memset(item, 0, sizeof(*item));
    if (token->kind == kSCRIPT_TOKEN_NUMBER && token->hex_digits > 0) {
        if (token->number > BYTE_VALUE_MAX) {
            return fail(self, "a data byte holds 0x00 to 0xFF");
        }
        item->kind = kSCRIPT_DATA_BYTE;
        item->byte = (uint8_t)token->number;
        return true;
    }
    if (ScriptToken_HexByte(token, &item->byte)) {
        item->kind = kSCRIPT_DATA_BYTE;
        return true;
    }
    if (token->kind == kSCRIPT_TOKEN_NAME && token->length <= NAME_LENGTH_MAX) {
        item->kind = kSCRIPT_DATA_NAME;
        item->name.text = token->text;
        item->name.length = (uint8_t)token->length;
        return true;
    }

    return fail(self, "data takes two-digit hexadecimal bytes, 0x numbers, constants and chunks");
}

static bool parseListReference(Parser *self, Cursor *cursor, ScriptListReference *reference)
{
    const ScriptToken *token = peek(cursor);

    memset(reference, 0, sizeof(*reference));
    if (token != NULL && token->kind == kSCRIPT_TOKEN_NAME) {
        return expectName(self, cursor, &reference->name, "expected a list");
    }

    return parseList(self, cursor, &reference->list);
}

static bool parseList(Parser *self, Cursor *cursor, ScriptList *list)
{
    ScriptNumber first;
    ScriptNumber last;
    ScriptRange *range;

    do {
        if (list->range_count == SCRIPT_LIST_RANGES_MAX) {
            return fail(self, "too many ranges in the list");
        }
        if (!expectNumber(self, cursor, &first, "expected a number or a range such as 0..31")) {
            return false;
        }
        last = first;
        if (acceptKind(cursor, kSCRIPT_TOKEN_RANGE) && !expectNumber(self, cursor, &last, "expected the end of the range")) {
            return false;
        }
        if (last.value < first.value || last.value > UINT32_MAX) {
            return fail(self, "a range goes from the low value to the high one");
        }
        range = &list->ranges[list->range_count++];
        range->first = (uint32_t)first.value;
        range->last = (uint32_t)last.value;
    } while (acceptKind(cursor, kSCRIPT_TOKEN_COMMA));

    return true;
}

static bool parseCondition(Parser *self, Cursor *cursor, uint16_t *condition_index)
{
    ScriptCondition *condition;

    if (self->script->condition_count == SCRIPT_CONDITIONS_MAX) {
        return fail(self, "too many conditions in the script");
    }

    condition = &self->script->conditions[self->script->condition_count];
    memset(condition, 0, sizeof(*condition));
    condition->line_number = cursor->line->line_number;
    do {
        if (condition->count == SCRIPT_CONDITION_TERMS_MAX) {
            return fail(self, "too many terms in one condition");
        }
        if (!parseTerm(self, cursor, &condition->terms[condition->count])) {
            return false;
        }
        condition->count++;
    } while (acceptWord(cursor, "and"));

    *condition_index = self->script->condition_count++;
    return true;
}

static bool parseTerm(Parser *self, Cursor *cursor, ScriptTerm *term)
{
    memset(term, 0, sizeof(*term));
    term->line_number = cursor->line->line_number;
    if (acceptKind(cursor, kSCRIPT_TOKEN_OPEN_PARENTHESIS)) {
        if (!parseShiftedField(self, cursor, term)) {
            return false;
        }
    } else if (!parseField(self, cursor, term)) {
        return false;
    }
    if (acceptKind(cursor, kSCRIPT_TOKEN_AMPERSAND)) {
        term->has_mask = true;
        if (!expectNumber(self, cursor, &term->mask, "expected a mask after &")) {
            return false;
        }
    }
    if (!expectKind(self, cursor, kSCRIPT_TOKEN_EQUALS, "expected ==")) {
        return false;
    }
    if (term->field == kSCRIPT_FIELD_BYTES) {
        return parseCheck(self, cursor, &term->check);
    }

    return parseValue(self, cursor, &term->value);
}

static bool parseField(Parser *self, Cursor *cursor, ScriptTerm *term)
{
    if (acceptWord(cursor, "id")) {
        term->field = kSCRIPT_FIELD_ID;
        return true;
    }
    if (acceptWord(cursor, "byte")) {
        return parseByteIndex(self, cursor, term);
    }

    return fail(self, "a condition compares id or byte[N]");
}

static bool parseShiftedField(Parser *self, Cursor *cursor, ScriptTerm *term)
{
    ScriptNumber shift;

    if (!parseField(self, cursor, term) ||
        !expectKind(self, cursor, kSCRIPT_TOKEN_SHIFT_RIGHT, "expected >> inside the parentheses") ||
        !expectNumber(self, cursor, &shift, "expected the shift") ||
        !expectKind(self, cursor, kSCRIPT_TOKEN_CLOSE_PARENTHESIS, "expected )")) {
        return false;
    }
    if (term->field == kSCRIPT_FIELD_BYTES || shift.value >= BYTE_VALUE_MAX) {
        return fail(self, "only id or one byte can be shifted");
    }

    term->shift = (uint8_t)shift.value;
    return true;
}

static bool parseByteIndex(Parser *self, Cursor *cursor, ScriptTerm *term)
{
    ScriptNumber first;
    ScriptNumber last;

    if (!expectKind(self, cursor, kSCRIPT_TOKEN_OPEN_BRACKET, "expected byte[N]") ||
        !expectNumber(self, cursor, &first, "expected byte[N]")) {
        return false;
    }
    last = first;
    term->field = kSCRIPT_FIELD_BYTE;
    if (acceptKind(cursor, kSCRIPT_TOKEN_RANGE)) {
        if (!expectNumber(self, cursor, &last, "expected byte[N..M]")) {
            return false;
        }
        term->field = kSCRIPT_FIELD_BYTES;
    }
    if (first.value > BYTE_VALUE_MAX || last.value > BYTE_VALUE_MAX || last.value < first.value) {
        return fail(self, "byte indexes go from low to high");
    }

    term->first = (uint8_t)first.value;
    term->last = (uint8_t)last.value;
    return expectKind(self, cursor, kSCRIPT_TOKEN_CLOSE_BRACKET, "expected ]");
}

static bool parseValue(Parser *self, Cursor *cursor, ScriptValue *value)
{
    const ScriptToken *token = peek(cursor);

    memset(value, 0, sizeof(*value));
    if (token == NULL) {
        return fail(self, "expected a value");
    }
    if (token->kind == kSCRIPT_TOKEN_NUMBER) {
        next(cursor);
        value->number.value = token->number;
        value->number.hex_digits = token->hex_digits;
        if (acceptKind(cursor, kSCRIPT_TOKEN_PLUS)) {
            return expectName(self, cursor, &value->name, "expected a name after +");
        }
        return true;
    }
    if (!expectName(self, cursor, &value->name, "expected a number or a name")) {
        return false;
    }
    if (acceptKind(cursor, kSCRIPT_TOKEN_PLUS)) {
        return expectNumber(self, cursor, &value->number, "expected a number after +");
    }

    return true;
}

static bool parseCheck(Parser *self, Cursor *cursor, ScriptCheck *check)
{
    const ScriptToken *token;

    memset(check, 0, sizeof(*check));
    if (acceptWord(cursor, "little_endian")) {
        check->order = kSCRIPT_BYTE_ORDER_LITTLE_ENDIAN;
    } else if (acceptWord(cursor, "big_endian")) {
        check->order = kSCRIPT_BYTE_ORDER_BIG_ENDIAN;
    } else {
        return fail(self, "several bytes compare to little_endian(...) or big_endian(...)");
    }
    if (!expectKind(self, cursor, kSCRIPT_TOKEN_OPEN_PARENTHESIS, "expected (")) {
        return false;
    }

    token = peek(cursor);
    if (token != NULL && token->kind == kSCRIPT_TOKEN_NUMBER) {
        if (!expectNumber(self, cursor, &check->literal, "expected a number")) {
            return false;
        }
    } else if (!expectName(self, cursor, &check->function, "expected a check such as sum16(block)") ||
               !expectKind(self, cursor, kSCRIPT_TOKEN_OPEN_PARENTHESIS, "expected (") ||
               !expectName(self, cursor, &check->data, "expected the data to check") ||
               !expectKind(self, cursor, kSCRIPT_TOKEN_CLOSE_PARENTHESIS, "expected )")) {
        return false;
    }

    return expectKind(self, cursor, kSCRIPT_TOKEN_CLOSE_PARENTHESIS, "expected )");
}

static uint16_t addStatement(Parser *self, uint8_t kind, uint16_t line_number)
{
    ScriptStatement *statement;
    uint16_t index = self->script->statement_count;

    if (index == SCRIPT_STATEMENTS_MAX) {
        fail(self, "too many statements");
        return SCRIPT_NO_INDEX;
    }

    statement = &self->script->statements[index];
    memset(statement, 0, sizeof(*statement));
    statement->kind = kind;
    statement->line_number = line_number;
    statement->first_child = SCRIPT_NO_INDEX;
    statement->next_sibling = SCRIPT_NO_INDEX;
    statement->where = SCRIPT_NO_INDEX;
    self->script->statement_count++;

    return index;
}

static void linkStatement(Parser *self, uint16_t index)
{
    Block *block = currentBlock(self);

    if (block->last_child != SCRIPT_NO_INDEX) {
        self->script->statements[block->last_child].next_sibling = index;
    } else if (block->kind == kBLOCK_ROOT) {
        self->script->first_statement = index;
    } else {
        self->script->statements[block->index].first_child = index;
    }
    block->last_child = index;
}

static bool fail(Parser *self, const char *message)
{
    BoundedString_Copy(self->error->message, sizeof(self->error->message), message);
    return false;
}

static const ScriptToken *peek(const Cursor *cursor)
{
    if (atEnd(cursor)) {
        return NULL;
    }
    return &cursor->line->tokens[cursor->position];
}

static const ScriptToken *next(Cursor *cursor)
{
    const ScriptToken *token = peek(cursor);

    if (token != NULL) {
        cursor->position++;
    }
    return token;
}

static bool atEnd(const Cursor *cursor)
{
    return cursor->position >= cursor->line->token_count;
}

static bool acceptWord(Cursor *cursor, const char *word)
{
    const ScriptToken *token = peek(cursor);

    if (token == NULL || !ScriptToken_Is(token, word)) {
        return false;
    }
    cursor->position++;
    return true;
}

static bool acceptKind(Cursor *cursor, uint8_t kind)
{
    const ScriptToken *token = peek(cursor);

    if (token == NULL || token->kind != kind) {
        return false;
    }
    cursor->position++;
    return true;
}

static bool expectWord(Parser *self, Cursor *cursor, const char *word, const char *message)
{
    if (!acceptWord(cursor, word)) {
        return fail(self, message);
    }
    return true;
}

static bool expectKind(Parser *self, Cursor *cursor, uint8_t kind, const char *message)
{
    if (!acceptKind(cursor, kind)) {
        return fail(self, message);
    }
    return true;
}

static bool expectName(Parser *self, Cursor *cursor, ScriptName *name, const char *message)
{
    const ScriptToken *token = peek(cursor);

    if (token == NULL || token->kind != kSCRIPT_TOKEN_NAME || token->length > NAME_LENGTH_MAX) {
        return fail(self, message);
    }

    name->text = token->text;
    name->length = (uint8_t)token->length;
    cursor->position++;
    return true;
}

static bool expectNumber(Parser *self, Cursor *cursor, ScriptNumber *number, const char *message)
{
    const ScriptToken *token = peek(cursor);

    if (token == NULL || token->kind != kSCRIPT_TOKEN_NUMBER) {
        return fail(self, message);
    }

    number->value = token->number;
    number->hex_digits = token->hex_digits;
    cursor->position++;
    return true;
}

static bool expectEnd(Parser *self, const Cursor *cursor)
{
    if (!atEnd(cursor)) {
        return fail(self, "unexpected words at the end of the line");
    }
    return true;
}
