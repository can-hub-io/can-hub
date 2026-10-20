#include "client/txn_script/script_compiler.h"

#include <string.h>

#include "protocol/frame_message.h"

#define MICROSECONDS_PER_MILLISECOND 1000
#define BYTE_VALUE_MAX 0xFF
#define BITS_PER_WORD 32
#define ROSTER_DISAGREES "every template for uses the same list and the same way to tell who answered"

typedef struct {
    ScriptProgram *program;
    const Script *script;
    ScriptBindings bindings;
    ScriptError *error;
    bool has_roster;
    TxnOriginExtractor origin;
    uint32_t roster[TXN_ROSTER_MAX];
    uint8_t roster_count;
} Compiler;

static const char *const RESERVED_NAMES[] = {
    "id", "byte", "and", "data", "send", "expect", "where", "for", "in", "of", "template", "exclusive",
    "success", "failure", "pending", "timeout", "then", "continue", "on", "mismatch", "abort", "gap",
    "read", "chunks", "crc", "sum8", "sum16", "sum32", "crc16", "crc32", "little_endian", "big_endian", "pad",
    "keepalive", "every", "idle",
};

static bool fail(Compiler *self, uint16_t line_number, const char *message);
static bool checkConstants(Compiler *self);
static bool isReserved(const ScriptName *name);
static bool checkTemplates(Compiler *self);
static bool compileTemplate(Compiler *self, uint16_t index);
static bool compileRoster(Compiler *self, uint16_t index, const ScriptTerm *term);
static bool rosterValues(Compiler *self, const ScriptTemplate *template_node, uint32_t *values, uint8_t *count);
static bool deriveOrigin(
    Compiler *self,
    const ScriptTerm *term,
    uint32_t highest,
    TxnOriginExtractor *origin,
    ScriptPattern *success
);
static bool agreesWithRoster(Compiler *self, const TxnOriginExtractor *origin, const uint32_t *values, uint8_t count);
static bool walk(Compiler *self, uint16_t first);
static bool walkFor(Compiler *self, const ScriptStatement *statement);
static bool checkSend(Compiler *self, const ScriptStatement *statement);
static bool checkChunkSource(Compiler *self, const ScriptStatement *statement);
static bool addStepVariant(Compiler *self, const ScriptStatement *statement);
static bool addVariant(Compiler *self, const ScriptVariantKey *key, uint16_t line_number);
static void buildVariant(const ScriptProgram *program, const ScriptVariantKey *key, TxnTemplateMessage *message);
static void toWirePattern(const ScriptPattern *pattern, TxnPattern *wire);
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

    if (!checkConstants(&compiler) || !checkTemplates(&compiler) || !walk(&compiler, script->first_statement)) {
        return false;
    }
    if (script->keepalive != SCRIPT_NO_INDEX && !checkSend(&compiler, &script->statements[script->keepalive])) {
        return false;
    }

    program->begin.flags = script->exclusive ? TXN_BEGIN_FLAG_EXCLUSIVE : 0;
    program->begin.idle_timeout_ms = script->idle_timeout_us / MICROSECONDS_PER_MILLISECOND;
    program->begin.keepalive.period_ms = script->keepalive_period_us / MICROSECONDS_PER_MILLISECOND;
    program->begin.origin = compiler.origin;
    program->begin.roster_count = compiler.roster_count;
    memcpy(program->begin.roster, compiler.roster, sizeof(compiler.roster));
    return true;
}

bool ScriptCompiler_StepShape(
    const ScriptProgram *program,
    const ScriptStatement *statement,
    const ScriptScope *scope,
    ScriptVariantKey *key,
    ScriptPattern *where,
    ScriptError *error
)
{
    memset(key, 0, sizeof(*key));
    memset(where, 0, sizeof(*where));
    key->base = SCRIPT_TRANSMIT_ONLY;
    key->gap_us = statement->gap_us;

    if (statement->has_expect) {
        key->base = ScriptCompiler_TemplateIndex(program->script, &statement->template_name);
        if (key->base == SCRIPT_NO_INDEX) {
            ScriptError_Set(error, statement->line_number, "no template has this name");
            return false;
        }
    }
    if (statement->where == SCRIPT_NO_INDEX) {
        return true;
    }
    if (!ScriptResolver_Condition(scope, &program->script->conditions[statement->where], where, NULL, error)) {
        return false;
    }
    if (program->script->templates[key->base].success == SCRIPT_NO_INDEX) {
        ScriptError_Set(error, statement->line_number, "where needs a template with a success");
        return false;
    }
    if (where->has_id && program->script->templates[key->base].has_roster) {
        ScriptError_Set(error, statement->line_number, "a template waiting for a list tells the answers apart by itself");
        return false;
    }

    key->where_mask = where->payload_mask;
    key->has_id_override = where->has_id;
    key->id_value = where->can_id;
    key->id_mask = where->can_mask;
    return true;
}

bool ScriptCompiler_FindVariant(const ScriptProgram *program, const ScriptVariantKey *key, uint8_t *template_id)
{
    uint8_t i;

    for(i=0; i<program->variant_count; i++) {
        if (memcmp(&program->variant_keys[i], key, sizeof(*key)) == 0) {
            *template_id = i;
            return true;
        }
    }
    return false;
}

uint16_t ScriptCompiler_TemplateIndex(const Script *script, const ScriptName *name)
{
    uint8_t i;

    for(i=0; i<script->template_count; i++) {
        if (ScriptName_Equals(&script->templates[i].name, name)) {
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

static bool checkTemplates(Compiler *self)
{
    uint8_t i;
    uint8_t j;

    for(i=0; i<self->script->template_count; i++) {
        for(j=0; j<i; j++) {
            if (ScriptName_Equals(&self->script->templates[j].name, &self->script->templates[i].name)) {
                return fail(self, self->script->templates[i].line_number, "this template is already defined");
            }
        }
        if (!compileTemplate(self, i)) {
            return false;
        }
    }
    return true;
}

static bool compileTemplate(Compiler *self, uint16_t index)
{
    const ScriptTemplate *template_node = &self->script->templates[index];
    ScriptScope scope = { self->script, NULL, NULL, NULL };
    ScriptPattern unused;
    const ScriptTerm *roster_term = NULL;

    if (!template_node->has_timeout) {
        return fail(self, template_node->line_number, "a template needs a timeout");
    }
    if (template_node->success != SCRIPT_NO_INDEX) {
        scope.roster_variable = template_node->has_roster ? &template_node->roster_variable : NULL;
        if (!ScriptResolver_Condition(&scope, &self->script->conditions[template_node->success],
                                      &self->program->success[index], &roster_term, self->error)) {
            return false;
        }
        scope.roster_variable = NULL;
    }
    if (template_node->failure != SCRIPT_NO_INDEX &&
        !ScriptResolver_Condition(&scope, &self->script->conditions[template_node->failure], &unused, NULL, self->error)) {
        return false;
    }
    if (template_node->pending != SCRIPT_NO_INDEX &&
        !ScriptResolver_Condition(&scope, &self->script->conditions[template_node->pending], &unused, NULL, self->error)) {
        return false;
    }
    if (!template_node->has_roster) {
        return true;
    }
    if (roster_term == NULL) {
        return fail(self, template_node->line_number, "the success of a template for compares the loop variable");
    }
    return compileRoster(self, index, roster_term);
}

static bool compileRoster(Compiler *self, uint16_t index, const ScriptTerm *term)
{
    const ScriptTemplate *template_node = &self->script->templates[index];
    TxnOriginExtractor origin;
    uint32_t values[TXN_ROSTER_MAX];
    uint32_t highest = 0;
    uint8_t count;
    uint8_t i;

    if (!rosterValues(self, template_node, values, &count)) {
        return false;
    }
    for(i=0; i<count; i++) {
        highest = values[i] > highest ? values[i] : highest;
    }
    if (template_node->quorum > count) {
        return fail(self, template_node->line_number, "the count is larger than the list");
    }
    if (!deriveOrigin(self, term, highest, &origin, &self->program->success[index])) {
        return false;
    }
    for(i=0; i<count; i++) {
        if ((((uint64_t)values[i] << origin.shift) & ~(uint64_t)origin.mask) != 0) {
            return fail(self, term->line_number, "a value of the list does not fit the mask");
        }
    }
    if (!agreesWithRoster(self, &origin, values, count)) {
        return fail(self, template_node->line_number, ROSTER_DISAGREES);
    }

    self->program->roster_variable = template_node->roster_variable;
    return true;
}

static bool rosterValues(Compiler *self, const ScriptTemplate *template_node, uint32_t *values, uint8_t *count)
{
    const ScriptList *list = listOf(self->script, &template_node->roster);
    uint32_t value;
    uint8_t i;

    *count = 0;
    if (list == NULL) {
        return fail(self, template_node->line_number, "the template for needs a list such as 0..31 or a list constant");
    }
    for(i=0; i<list->range_count; i++) {
        for(value=list->ranges[i].first; ; value++) {
            if (*count == TXN_ROSTER_MAX) {
                return fail(self, template_node->line_number, "a template for waits for at most 64 values");
            }
            values[(*count)++] = value;
            if (value == list->ranges[i].last) {
                break;
            }
        }
    }
    return true;
}

static bool deriveOrigin(
    Compiler *self,
    const ScriptTerm *term,
    uint32_t highest,
    TxnOriginExtractor *origin,
    ScriptPattern *success
)
{
    uint32_t variable_mask = ((uint32_t)1 << bitsFor(highest)) - 1;
    uint32_t base = (uint32_t)term->value.number.value;
    bool extended = term->value.number.hex_digits >= SCRIPT_EXTENDED_HEX_DIGITS || base > SCRIPT_STANDARD_ID_MAX;

    memset(origin, 0, sizeof(*origin));
    origin->shift = term->shift;
    if (term->field == kSCRIPT_FIELD_BYTE) {
        origin->source = kTXN_ORIGIN_SOURCE_PAYLOAD;
        origin->payload_offset = term->first;
        origin->mask = (uint32_t)(term->has_mask ? term->mask.value : BYTE_VALUE_MAX) << term->shift;
        return true;
    }

    origin->source = kTXN_ORIGIN_SOURCE_CAN_ID;
    if (term->has_mask) {
        origin->mask = (uint32_t)term->mask.value << term->shift;
        return true;
    }
    if (base == 0) {
        origin->mask = FRAME_CAN_ID_MASK;
        return true;
    }
    if ((base & variable_mask) != 0) {
        return fail(self, term->line_number, "the base must leave the variable's bits clear, such as 0x700 + node");
    }

    origin->mask = variable_mask;
    success->has_id = true;
    success->extended = success->extended || extended;
    success->can_mask |= (extended ? FRAME_CAN_ID_MASK : SCRIPT_STANDARD_ID_MAX) & ~variable_mask;
    success->can_id |= base;
    success->can_mask |= FRAME_CAN_ID_FLAG_EFF;
    if (success->extended) {
        success->can_id |= FRAME_CAN_ID_FLAG_EFF;
    }
    return true;
}

static bool agreesWithRoster(Compiler *self, const TxnOriginExtractor *origin, const uint32_t *values, uint8_t count)
{
    if (!self->has_roster) {
        self->has_roster = true;
        self->origin = *origin;
        self->roster_count = count;
        memcpy(self->roster, values, (size_t)count * sizeof(values[0]));
        return true;
    }
    return memcmp(&self->origin, origin, sizeof(*origin)) == 0 && self->roster_count == count &&
           memcmp(self->roster, values, (size_t)count * sizeof(values[0])) == 0;
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
        if (!addStepVariant(self, statement)) {
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

static bool addStepVariant(Compiler *self, const ScriptStatement *statement)
{
    ScriptScope scope = { self->script, &self->bindings, NULL, NULL };
    ScriptVariantKey key;
    ScriptPattern where;
    uint8_t template_id;

    if (!ScriptCompiler_StepShape(self->program, statement, &scope, &key, &where, self->error)) {
        return false;
    }
    if (ScriptCompiler_FindVariant(self->program, &key, &template_id)) {
        return true;
    }
    return addVariant(self, &key, statement->line_number);
}

static bool addVariant(Compiler *self, const ScriptVariantKey *key, uint16_t line_number)
{
    ScriptProgram *program = self->program;

    if (program->variant_count == TXN_TEMPLATES_MAX) {
        return fail(self, line_number, "more than 16 templates, counting what where and gap add to them");
    }

    program->variant_keys[program->variant_count] = *key;
    buildVariant(program, key, &program->variants[program->variant_count]);
    program->variants[program->variant_count].template_id = program->variant_count;
    program->variant_count++;
    return true;
}

static void buildVariant(const ScriptProgram *program, const ScriptVariantKey *key, TxnTemplateMessage *message)
{
    const ScriptTemplate *template_node;
    ScriptPattern success;
    ScriptPattern other;
    ScriptScope scope = { program->script, NULL, NULL, NULL };
    ScriptError ignored;

    memset(message, 0, sizeof(*message));
    message->gap_us = key->gap_us;
    if (key->base == SCRIPT_TRANSMIT_ONLY) {
        message->advance_mode = kTXN_ADVANCE_MODE_NONE;
        return;
    }

    template_node = &program->script->templates[key->base];
    message->advance_mode = kTXN_ADVANCE_MODE_COUNT;
    message->count_k = 1;
    if (template_node->has_roster) {
        message->advance_mode = template_node->quorum > 0 ? kTXN_ADVANCE_MODE_ROSTER_K : kTXN_ADVANCE_MODE_ROSTER_ALL;
        message->count_k = template_node->quorum;
    }
    message->timeout_ms = timeoutMilliseconds(template_node->timeout_us);
    message->flags = (uint8_t)((template_node->continue_on_timeout ? TXN_TEMPLATE_FLAG_SOFT : 0) |
                               (template_node->mismatch_abort ? TXN_TEMPLATE_FLAG_MISMATCH_ABORT : 0));

    if (template_node->success != SCRIPT_NO_INDEX) {
        success = program->success[key->base];
        success.payload_mask |= key->where_mask;
        if (key->has_id_override) {
            success.can_id = key->id_value;
            success.can_mask = key->id_mask;
        }
        toWirePattern(&success, &message->patterns[kTXN_PATTERN_ADVANCE]);
        message->patterns_present |= 1u << kTXN_PATTERN_ADVANCE;
    }
    if (template_node->failure != SCRIPT_NO_INDEX &&
        ScriptResolver_Condition(&scope, &program->script->conditions[template_node->failure], &other, NULL, &ignored)) {
        toWirePattern(&other, &message->patterns[kTXN_PATTERN_ABORT]);
        message->patterns_present |= 1u << kTXN_PATTERN_ABORT;
    }
    if (template_node->pending != SCRIPT_NO_INDEX &&
        ScriptResolver_Condition(&scope, &program->script->conditions[template_node->pending], &other, NULL, &ignored)) {
        toWirePattern(&other, &message->patterns[kTXN_PATTERN_EXTEND]);
        message->patterns_present |= 1u << kTXN_PATTERN_EXTEND;
    }
}

static void toWirePattern(const ScriptPattern *pattern, TxnPattern *wire)
{
    wire->can_id = pattern->can_id;
    wire->can_mask = pattern->can_mask;
    wire->payload_mask = pattern->payload_mask;
    wire->payload_value = pattern->payload_value;
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
