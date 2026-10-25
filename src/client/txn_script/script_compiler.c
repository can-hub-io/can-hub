#include "client/txn_script/script_compiler.h"

#include <string.h>

#include "protocol/frame_message.h"

#define MICROSECONDS_PER_MILLISECOND 1000
#define BITS_PER_BYTE 8
#define BYTE_VALUE_MAX 0xFF
#define BITS_PER_WORD 32
#define ROSTER_DISAGREES "every response for uses the same list and the same way to tell who answered"

typedef struct {
    ScriptProgram *program;
    const Script *script;
    ScriptBindings bindings;
    ScriptError *error;
    bool has_roster;
    CanHubTxnNodeSource node_from;
    uint8_t node_byte;
    uint32_t node_bits;
} Compiler;

static const char *const RESERVED_NAMES[] = {
    "id", "byte", "and", "data", "send", "expect", "where", "for", "in", "of", "response", "exclusive",
    "success", "failure", "pending", "timeout", "after", "then", "continue", "on", "mismatch", "abort", "gap",
    "read", "chunks", "crc", "sum8", "sum16", "sum32", "crc16", "crc32", "little_endian", "big_endian", "pad",
    "keepalive", "every", "idle",
};

static bool fail(Compiler *self, uint16_t line_number, const char *message);
static bool checkConstants(Compiler *self);
static bool isReserved(const ScriptName *name);
static bool checkResponses(Compiler *self);
static bool compileResponse(Compiler *self, uint16_t index);
static bool compileOther(Compiler *self, uint16_t condition, CanHubTxnMatch *match);
static bool compileRoster(Compiler *self, uint16_t index, const ScriptTerm *term, ScriptPattern *success);
static bool nodeBitsAreFree(Compiler *self, const CanHubTxnResponse *response, const ScriptTerm *term);
static bool rosterValues(Compiler *self, const ScriptResponse *response_node, uint32_t *values, uint8_t *count);
static bool deriveNodeBits(Compiler *self, const ScriptTerm *term, uint32_t highest, ScriptPattern *success, uint32_t *bits);
static bool agreesWithRoster(Compiler *self, const CanHubTxnResponse *response, uint32_t bits, const uint32_t *values, uint8_t count);
static bool walk(Compiler *self, uint16_t first);
static bool walkFor(Compiler *self, const ScriptStatement *statement);
static bool checkSend(Compiler *self, const ScriptStatement *statement);
static bool checkChunkSource(Compiler *self, const ScriptStatement *statement);
static bool checkStep(Compiler *self, const ScriptStatement *statement);
static void toMatch(const ScriptPattern *pattern, CanHubTxnMatch *match);
static uint32_t timeoutMilliseconds(uint32_t timeout_us);
static bool pushBinding(Compiler *self, const ScriptName *name, uint8_t kind, uint32_t value, uint16_t line_number);
static const ScriptList *listOf(const Script *script, const ScriptListReference *reference);
static uint8_t bitsFor(uint32_t value);
static bool isByteConstant(const ScriptConstant *constant);

/* ---------- public ---------- */

bool ScriptCompiler_Compile(ScriptProgram *program, const Script *script, ScriptError *error)
{
    Compiler compiler;

    memset(program, 0, sizeof(*program));
    memset(&compiler, 0, sizeof(compiler));
    program->script = script;
    compiler.program = program;
    compiler.script = script;
    compiler.error = error;

    if (!checkConstants(&compiler) || !checkResponses(&compiler) || !walk(&compiler, script->first_statement)) {
        return false;
    }
    if (script->keepalive != SCRIPT_NO_INDEX && !checkSend(&compiler, &script->statements[script->keepalive])) {
        return false;
    }

    program->options.exclusive = script->exclusive;
    program->options.idle_timeout_ms = script->idle_timeout_us / MICROSECONDS_PER_MILLISECOND;
    if (program->options.idle_timeout_ms == 0) {
        program->options.idle_timeout_ms = SCRIPT_DEFAULT_IDLE_TIMEOUT_MS;
    }
    return true;
}

bool ScriptCompiler_StepShape(
    const ScriptProgram *program,
    const ScriptStatement *statement,
    const ScriptScope *scope,
    uint16_t *response,
    CanHubTxnMatch *where,
    ScriptError *error
)
{
    const CanHubTxnResponse *response_definition;
    ScriptPattern pattern;

    *response = SCRIPT_TRANSMIT_ONLY;
    memset(where, 0, sizeof(*where));
    if (statement->has_expect) {
        *response = ScriptCompiler_ResponseIndex(program->script, &statement->response_name);
        if (*response == SCRIPT_NO_INDEX) {
            ScriptError_Set(error, statement->line_number, "no response has this name");
            return false;
        }
    }
    if (statement->where == SCRIPT_NO_INDEX) {
        return true;
    }
    if (!ScriptResolver_Condition(scope, &program->script->conditions[statement->where], &pattern, NULL, error)) {
        return false;
    }

    response_definition = &program->responses[*response];
    if (pattern.has_id && response_definition->nodes != NULL) {
        ScriptError_Set(error, statement->line_number, "a response waiting for a list tells the answers apart by itself");
        return false;
    }
    toMatch(&pattern, where);
    if (!pattern.has_id) {
        where->id = response_definition->success.id;
        where->id_ignored_bits = response_definition->success.id_ignored_bits;
    }
    return true;
}

uint16_t ScriptCompiler_ResponseIndex(const Script *script, const ScriptName *name)
{
    uint8_t i;

    for(i=0; i<script->response_count; i++) {
        if (ScriptName_Equals(&script->responses[i].name, name)) {
            return i;
        }
    }
    return SCRIPT_NO_INDEX;
}

/* ---------- private ---------- */

static bool fail(Compiler *self, uint16_t line_number, const char *message)
{
    ScriptError_Set(self->error, line_number, message);
    return false;
}

static bool checkConstants(Compiler *self)
{
    const ScriptConstant *constant;
    uint8_t i;
    uint8_t j;

    for(i=0; i<self->script->constant_count; i++) {
        constant = &self->script->constants[i];
        if (isReserved(&constant->name)) {
            return fail(self, constant->line_number, "this name is a word of the language");
        }
        for(j=0; j<i; j++) {
            if (ScriptName_Equals(&self->script->constants[j].name, &constant->name)) {
                return fail(self, constant->line_number, "this constant is already defined");
            }
        }
        if (constant->kind == kSCRIPT_CONSTANT_CRC && !ChecksumModel_IsValid(&constant->model)) {
            return fail(self, constant->line_number, "a crc is 8 to 32 bits wide");
        }
    }
    return true;
}

static bool isReserved(const ScriptName *name)
{
    uint8_t i;

    for(i=0; i<sizeof(RESERVED_NAMES) / sizeof(RESERVED_NAMES[0]); i++) {
        if (ScriptName_Is(name, RESERVED_NAMES[i])) {
            return true;
        }
    }
    return false;
}

static bool checkResponses(Compiler *self)
{
    uint8_t i;
    uint8_t j;

    for(i=0; i<self->script->response_count; i++) {
        for(j=0; j<i; j++) {
            if (ScriptName_Equals(&self->script->responses[j].name, &self->script->responses[i].name)) {
                return fail(self, self->script->responses[i].line_number, "this response is already defined");
            }
        }
        if (!compileResponse(self, i)) {
            return false;
        }
    }
    return true;
}

static bool compileResponse(Compiler *self, uint16_t index)
{
    const ScriptResponse *response_node = &self->script->responses[index];
    CanHubTxnResponse *response = &self->program->responses[index];
    ScriptScope scope = { self->script, NULL, NULL, NULL };
    const ScriptTerm *roster_term = NULL;
    const ScriptCondition *condition;
    ScriptPattern success;

    if (!response_node->has_timeout) {
        return fail(self, response_node->line_number, "a response needs a timeout");
    }
    if (response_node->success == SCRIPT_NO_INDEX) {
        return fail(self, response_node->line_number, "a response needs a success line");
    }

    scope.roster_variable = response_node->has_roster ? &response_node->roster_variable : NULL;
    condition = &self->script->conditions[response_node->success];
    if (!ScriptResolver_Condition(&scope, condition, &success, &roster_term, self->error)) {
        return false;
    }
    if (response_node->has_roster && roster_term == NULL) {
        return fail(self, response_node->line_number, "the success of a response for compares the loop variable");
    }
    if (response_node->has_roster && !compileRoster(self, index, roster_term, &success)) {
        return false;
    }
    toMatch(&success, &response->success);
    if (response_node->has_roster && !nodeBitsAreFree(self, response, roster_term)) {
        return false;
    }
    if (response_node->failure != SCRIPT_NO_INDEX) {
        if (!compileOther(self, response_node->failure, &self->program->failures[index])) {
            return false;
        }
        response->failure = &self->program->failures[index];
    }
    if (response_node->pending != SCRIPT_NO_INDEX) {
        if (!compileOther(self, response_node->pending, &self->program->pendings[index])) {
            return false;
        }
        response->pending = &self->program->pendings[index];
    }

    response->timeout_ms = timeoutMilliseconds(response_node->timeout_us);
    response->timeout_after_pending_ms = response->timeout_ms;
    if (response_node->timeout_after_pending_us > 0) {
        response->timeout_after_pending_ms = timeoutMilliseconds(response_node->timeout_after_pending_us);
    }
    response->continue_on_timeout = response_node->continue_on_timeout;
    response->on_mismatch_abort = response_node->mismatch_abort;
    return true;
}

static bool compileOther(Compiler *self, uint16_t condition, CanHubTxnMatch *match)
{
    ScriptScope scope = { self->script, NULL, NULL, NULL };
    ScriptPattern pattern;

    if (!ScriptResolver_Condition(&scope, &self->script->conditions[condition], &pattern, NULL, self->error)) {
        return false;
    }
    toMatch(&pattern, match);
    return true;
}

static bool compileRoster(Compiler *self, uint16_t index, const ScriptTerm *term, ScriptPattern *success)
{
    const ScriptResponse *response_node = &self->script->responses[index];
    CanHubTxnResponse *response = &self->program->responses[index];
    uint32_t values[CANHUB_TXN_ROSTER_MAX];
    uint32_t highest = 0;
    uint32_t bits;
    uint8_t count;
    uint8_t i;

    if (!rosterValues(self, response_node, values, &count)) {
        return false;
    }
    for(i=0; i<count; i++) {
        highest = values[i] > highest ? values[i] : highest;
    }
    if (response_node->quorum > count) {
        return fail(self, response_node->line_number, "the count is larger than the list");
    }

    if (term->field == kSCRIPT_FIELD_BYTE) {
        if ((term->has_mask && term->mask.value != BYTE_VALUE_MAX) || term->shift != 0) {
            return fail(self, term->line_number, "a node told by a data byte is the whole byte, such as byte[2] == cell");
        }
        response->node_from = CANHUB_TXN_NODE_IN_BYTE;
        response->node_byte = term->first;
        bits = BYTE_VALUE_MAX;
    } else if (!deriveNodeBits(self, term, highest, success, &bits)) {
        return false;
    }
    for(i=0; i<count; i++) {
        if (term->field != kSCRIPT_FIELD_BYTE && (((uint64_t)values[i] << term->shift) & ~(uint64_t)bits) != 0) {
            return fail(self, term->line_number, "a value of the list does not fit the mask");
        }
        if (term->field == kSCRIPT_FIELD_BYTE && values[i] > BYTE_VALUE_MAX) {
            return fail(self, term->line_number, "a value of the list does not fit the byte");
        }
    }

    response->nodes = self->program->nodes;
    response->node_count = count;
    response->allowed_missing = response_node->quorum > 0 ? count - response_node->quorum : 0;
    if (!agreesWithRoster(self, response, bits, values, count)) {
        return fail(self, response_node->line_number, ROSTER_DISAGREES);
    }
    return true;
}

static bool nodeBitsAreFree(Compiler *self, const CanHubTxnResponse *response, const ScriptTerm *term)
{
    if (response->node_from == CANHUB_TXN_NODE_IN_BYTE || response->success.id_ignored_bits == self->node_bits) {
        return true;
    }
    return fail(self, term->line_number, "the bits of the loop variable must be the only bits of the id left free, such as id & 0x7F0 == 0x700 and id & 0x0F == node");
}

static bool rosterValues(Compiler *self, const ScriptResponse *response_node, uint32_t *values, uint8_t *count)
{
    const ScriptList *list = listOf(self->script, &response_node->roster);
    uint32_t value;
    uint8_t i;

    *count = 0;
    if (list == NULL) {
        return fail(self, response_node->line_number, "the response for needs a list such as 0..31 or a list constant");
    }
    for(i=0; i<list->range_count; i++) {
        for(value=list->ranges[i].first; ; value++) {
            if (*count == CANHUB_TXN_ROSTER_MAX) {
                return fail(self, response_node->line_number, "a response for waits for at most 64 values");
            }
            values[(*count)++] = value;
            if (value == list->ranges[i].last) {
                break;
            }
        }
    }
    return true;
}

static bool deriveNodeBits(Compiler *self, const ScriptTerm *term, uint32_t highest, ScriptPattern *success, uint32_t *bits)
{
    uint32_t variable_mask = ((uint32_t)1 << bitsFor(highest)) - 1;
    uint32_t base = (uint32_t)term->value.number.value;
    bool extended = term->value.number.hex_digits >= SCRIPT_EXTENDED_HEX_DIGITS || base > SCRIPT_STANDARD_ID_MAX;

    if (term->has_mask) {
        *bits = (uint32_t)term->mask.value << term->shift;
        return true;
    }
    if ((base & variable_mask) != 0) {
        return fail(self, term->line_number, "the base must leave the variable's bits clear, such as 0x700 + node");
    }

    *bits = variable_mask;
    success->has_id = true;
    success->extended = success->extended || extended;
    success->can_mask |= (success->extended ? FRAME_CAN_ID_MASK : SCRIPT_STANDARD_ID_MAX) & ~variable_mask;
    success->can_id |= base;
    success->can_mask |= FRAME_CAN_ID_FLAG_EFF;
    if (success->extended) {
        success->can_id |= FRAME_CAN_ID_FLAG_EFF;
    }
    return true;
}

static bool agreesWithRoster(Compiler *self, const CanHubTxnResponse *response, uint32_t bits, const uint32_t *values, uint8_t count)
{
    ScriptProgram *program = self->program;

    if (!self->has_roster) {
        self->has_roster = true;
        self->node_from = response->node_from;
        self->node_byte = response->node_byte;
        self->node_bits = bits;
        program->node_count = count;
        memcpy(program->nodes, values, (size_t)count * sizeof(values[0]));
        return true;
    }
    return self->node_from == response->node_from && self->node_byte == response->node_byte &&
           self->node_bits == bits && program->node_count == count && memcmp(program->nodes, values, (size_t)count * sizeof(values[0])) == 0;
}

static bool walk(Compiler *self, uint16_t first)
{
    const ScriptStatement *statement;
    uint16_t index;

    for(index=first; index!=SCRIPT_NO_INDEX; index=statement->next_sibling) {
        statement = &self->script->statements[index];
        if (statement->kind == kSCRIPT_STATEMENT_FOR) {
            if (!walkFor(self, statement)) {
                return false;
            }
            continue;
        }
        if (statement->kind == kSCRIPT_STATEMENT_SEND && !checkSend(self, statement)) {
            return false;
        }
        if (!checkStep(self, statement)) {
            return false;
        }
    }
    return true;
}

static bool walkFor(Compiler *self, const ScriptStatement *statement)
{
    const ScriptList *list;
    uint32_t value;
    uint8_t i;

    if (statement->iterable == kSCRIPT_ITERABLE_CHUNKS) {
        if (!checkChunkSource(self, statement) ||
            !pushBinding(self, &statement->variable, kSCRIPT_BINDING_CHUNK, 0, statement->line_number) ||
            !walk(self, statement->first_child)) {
            return false;
        }
        self->bindings.count--;
        return true;
    }

    list = listOf(self->script, &statement->list);
    if (list == NULL) {
        return fail(self, statement->line_number, "for goes over a list such as 0..15, a list constant or chunks()");
    }
    for(i=0; i<list->range_count; i++) {
        for(value=list->ranges[i].first; ; value++) {
            if (!pushBinding(self, &statement->variable, kSCRIPT_BINDING_VALUE, value, statement->line_number) ||
                !walk(self, statement->first_child)) {
                return false;
            }
            self->bindings.count--;
            if (value == list->ranges[i].last) {
                break;
            }
        }
    }
    return true;
}

static bool checkSend(Compiler *self, const ScriptStatement *statement)
{
    ScriptScope scope = { self->script, &self->bindings, NULL, NULL };
    const ScriptDataItem *item;
    const ScriptBinding *binding;
    const ScriptConstant *constant;
    uint32_t value;
    uint8_t i;

    self->error->line_number = statement->line_number;
    if (!ScriptResolver_Value(&scope, &statement->id, &value, NULL, self->error)) {
        return false;
    }
    if (value > FRAME_CAN_ID_MASK) {
        return fail(self, statement->line_number, "an identifier has 29 bits");
    }

    for(i=0; i<statement->data_count; i++) {
        item = &self->script->data_items[statement->first_data_item + i];
        if (item->kind == kSCRIPT_DATA_BYTE) {
            continue;
        }
        binding = ScriptResolver_Binding(&self->bindings, &item->name);
        constant = ScriptResolver_Constant(self->script, &item->name);
        if (binding != NULL && (binding->kind == kSCRIPT_BINDING_CHUNK || binding->value <= BYTE_VALUE_MAX)) {
            continue;
        }
        if (binding == NULL && isByteConstant(constant)) {
            continue;
        }
        return fail(self, statement->line_number, "data takes bytes, one-byte constants and variables, and chunks");
    }
    return true;
}

static bool checkChunkSource(Compiler *self, const ScriptStatement *statement)
{
    const ScriptBinding *binding = ScriptResolver_Binding(&self->bindings, &statement->chunk_source);
    const ScriptConstant *constant = ScriptResolver_Constant(self->script, &statement->chunk_source);

    if (statement->chunk_size == 0 || statement->chunk_size > SCRIPT_CHUNK_SIZE_MAX) {
        return fail(self, statement->line_number, "chunks are 1 to 4096 bytes");
    }
    if (binding != NULL && binding->kind == kSCRIPT_BINDING_CHUNK) {
        return true;
    }
    if (binding == NULL && constant != NULL && constant->kind == kSCRIPT_CONSTANT_FILE) {
        return true;
    }
    return fail(self, statement->line_number, "chunks() splits a read() constant or another chunk");
}

static bool checkStep(Compiler *self, const ScriptStatement *statement)
{
    ScriptScope scope = { self->script, &self->bindings, NULL, NULL };
    CanHubTxnMatch where;
    uint16_t response;

    return ScriptCompiler_StepShape(self->program, statement, &scope, &response, &where, self->error);
}

static void toMatch(const ScriptPattern *pattern, CanHubTxnMatch *match)
{
    uint32_t width = pattern->extended ? CANHUB_EXTENDED_ID_MASK : CANHUB_STANDARD_ID_MASK;
    uint8_t i;

    memset(match, 0, sizeof(*match));
    match->id.value = pattern->can_id & width;
    match->id.extended = pattern->extended;
    match->id_ignored_bits = pattern->has_id ? width & ~pattern->can_mask : CANHUB_TXN_ANY_ID_BITS;
    for(i=0; i<CANHUB_TXN_MATCH_BYTES; i++) {
        match->bytes[i].mask = (uint8_t)(pattern->payload_mask >> (i * BITS_PER_BYTE));
        match->bytes[i].value = (uint8_t)(pattern->payload_value >> (i * BITS_PER_BYTE));
    }
}

static uint32_t timeoutMilliseconds(uint32_t timeout_us)
{
    return (timeout_us + MICROSECONDS_PER_MILLISECOND - 1) / MICROSECONDS_PER_MILLISECOND;
}

static bool pushBinding(Compiler *self, const ScriptName *name, uint8_t kind, uint32_t value, uint16_t line_number)
{
    ScriptBinding *binding;

    if (self->bindings.count == SCRIPT_BINDINGS_MAX) {
        return fail(self, line_number, "loops nested too deep");
    }
    if (isReserved(name) || ScriptResolver_Binding(&self->bindings, name) != NULL) {
        return fail(self, line_number, "this loop variable name is already in use");
    }

    binding = &self->bindings.items[self->bindings.count++];
    memset(binding, 0, sizeof(*binding));
    binding->name = *name;
    binding->kind = kind;
    binding->value = value;
    return true;
}

static const ScriptList *listOf(const Script *script, const ScriptListReference *reference)
{
    const ScriptConstant *constant;

    if (reference->name.length == 0) {
        return reference->list.range_count > 0 ? &reference->list : NULL;
    }
    constant = ScriptResolver_Constant(script, &reference->name);
    if (constant == NULL || constant->kind != kSCRIPT_CONSTANT_LIST) {
        return NULL;
    }
    return &constant->list;
}

static uint8_t bitsFor(uint32_t value)
{
    uint8_t bits = 0;

    while (bits < BITS_PER_WORD && (value >> bits) != 0) {
        bits++;
    }
    return bits;
}

static bool isByteConstant(const ScriptConstant *constant)
{
    return constant != NULL && constant->kind == kSCRIPT_CONSTANT_NUMBER && constant->number.value <= BYTE_VALUE_MAX;
}
