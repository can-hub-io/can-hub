#include "client/txn_script/script_resolver.h"

#include <string.h>

#include "protocol/frame_message.h"
#include "protocol/txn_message.h"

#define BITS_PER_BYTE 8
#define BYTE_MASK 0xFF
#define CHECK_BYTES_MAX 4
#define FILE_READ_SIZE 4096

typedef struct {
    const char *name;
    const ChecksumModel *model;
} BuiltinCheck;

static const BuiltinCheck BUILTIN_CHECKS[] = {
    { "sum8", &CHECKSUM_SUM8 },
    { "sum16", &CHECKSUM_SUM16 },
    { "sum32", &CHECKSUM_SUM32 },
    { "crc16", &CHECKSUM_CRC16 },
    { "crc32", &CHECKSUM_CRC32 },
};

static bool fail(ScriptError *error, uint16_t line_number, const char *message);
static bool isRosterTerm(const ScriptScope *scope, const ScriptTerm *term);
static bool addIdTerm(const ScriptScope *scope, const ScriptTerm *term, ScriptPattern *pattern, ScriptError *error);
static bool addByteTerm(const ScriptScope *scope, const ScriptTerm *term, ScriptPattern *pattern, ScriptError *error);
static bool addBytesTerm(const ScriptScope *scope, const ScriptTerm *term, ScriptPattern *pattern, ScriptError *error);
static bool checkValue(const ScriptScope *scope, const ScriptTerm *term, uint32_t *value, ScriptError *error);
static bool checkOverData(const ScriptScope *scope, const ScriptName *data, const ChecksumModel *model, uint32_t *value);
static bool checkOverFile(const ScriptScope *scope, const ScriptConstant *file, const ChecksumModel *model, uint32_t *value);
static bool mergeId(ScriptPattern *pattern, uint32_t value, uint32_t mask);
static bool mergePayload(ScriptPattern *pattern, uint64_t value, uint64_t mask);
static uint8_t checkBytes(const ChecksumModel *model);

/* ---------- public ---------- */

const ScriptConstant *ScriptResolver_Constant(const Script *script, const ScriptName *name)
{
    uint8_t i;

    for(i=0; i<script->constant_count; i++) {
        if (ScriptName_Equals(&script->constants[i].name, name)) {
            return &script->constants[i];
        }
    }
    return NULL;
}

const ScriptBinding *ScriptResolver_Binding(const ScriptBindings *bindings, const ScriptName *name)
{
    uint8_t i;

    if (bindings == NULL) {
        return NULL;
    }
    for(i=bindings->count; i>0; i--) {
        if (ScriptName_Equals(&bindings->items[i - 1].name, name)) {
            return &bindings->items[i - 1];
        }
    }
    return NULL;
}

bool ScriptResolver_Value(
    const ScriptScope *scope,
    const ScriptValue *value,
    uint32_t *resolved,
    bool *extended,
    ScriptError *error
)
{
    const ScriptBinding *binding;
    const ScriptConstant *constant;
    uint64_t total = value->number.value;
    bool long_form = value->number.hex_digits >= SCRIPT_EXTENDED_HEX_DIGITS;

    if (value->name.length > 0) {
        binding = ScriptResolver_Binding(scope->bindings, &value->name);
        constant = ScriptResolver_Constant(scope->script, &value->name);
        if (binding != NULL && binding->kind == kSCRIPT_BINDING_VALUE) {
            total += binding->value;
        } else if (binding == NULL && constant != NULL && constant->kind == kSCRIPT_CONSTANT_NUMBER) {
            total += constant->number.value;
            long_form = long_form || constant->number.hex_digits >= SCRIPT_EXTENDED_HEX_DIGITS;
        } else {
            return fail(error, error->line_number, "this name is not a number here");
        }
    }
    if (total > UINT32_MAX) {
        return fail(error, error->line_number, "the value does not fit 32 bits");
    }

    *resolved = (uint32_t)total;
    if (extended != NULL) {
        *extended = long_form || total > SCRIPT_STANDARD_ID_MAX;
    }
    return true;
}

bool ScriptResolver_Condition(
    const ScriptScope *scope,
    const ScriptCondition *condition,
    ScriptPattern *pattern,
    const ScriptTerm **roster_term,
    ScriptError *error
)
{
    const ScriptTerm *term;
    uint8_t i;

    memset(pattern, 0, sizeof(*pattern));
    if (roster_term != NULL) {
        *roster_term = NULL;
    }

    for(i=0; i<condition->count; i++) {
        term = &condition->terms[i];
        error->line_number = term->line_number;
        if (isRosterTerm(scope, term)) {
            if (roster_term == NULL || *roster_term != NULL) {
                return fail(error, term->line_number, "the loop variable is compared once, in the template's success");
            }
            *roster_term = term;
            continue;
        }
        if (term->field == kSCRIPT_FIELD_ID && !addIdTerm(scope, term, pattern, error)) {
            return false;
        }
        if (term->field == kSCRIPT_FIELD_BYTE && !addByteTerm(scope, term, pattern, error)) {
            return false;
        }
        if (term->field == kSCRIPT_FIELD_BYTES && !addBytesTerm(scope, term, pattern, error)) {
            return false;
        }
    }

    if (pattern->has_id) {
        pattern->can_mask |= FRAME_CAN_ID_FLAG_EFF;
        if (pattern->extended) {
            pattern->can_id |= FRAME_CAN_ID_FLAG_EFF;
        }
    }
    return true;
}

bool ScriptResolver_CheckModel(const Script *script, const ScriptName *function, ChecksumModel *model)
{
    const ScriptConstant *constant;
    uint8_t i;

    for(i=0; i<sizeof(BUILTIN_CHECKS) / sizeof(BUILTIN_CHECKS[0]); i++) {
        if (ScriptName_Is(function, BUILTIN_CHECKS[i].name)) {
            *model = *BUILTIN_CHECKS[i].model;
            return true;
        }
    }

    constant = ScriptResolver_Constant(script, function);
    if (constant == NULL || constant->kind != kSCRIPT_CONSTANT_CRC) {
        return false;
    }
    *model = constant->model;
    return true;
}

/* ---------- private ---------- */

static bool fail(ScriptError *error, uint16_t line_number, const char *message)
{
    ScriptError_Set(error, line_number, message);
    return false;
}

static bool isRosterTerm(const ScriptScope *scope, const ScriptTerm *term)
{
    if (scope->roster_variable == NULL || term->field == kSCRIPT_FIELD_BYTES) {
        return false;
    }
    return term->value.name.length > 0 && ScriptName_Equals(&term->value.name, scope->roster_variable);
}

static bool addIdTerm(const ScriptScope *scope, const ScriptTerm *term, ScriptPattern *pattern, ScriptError *error)
{
    uint32_t value;
    uint32_t mask;
    bool extended;

    if (!ScriptResolver_Value(scope, &term->value, &value, &extended, error)) {
        return false;
    }

    extended = extended || (term->has_mask && term->mask.hex_digits >= SCRIPT_EXTENDED_HEX_DIGITS);
    mask = term->has_mask ? (uint32_t)term->mask.value : (extended ? FRAME_CAN_ID_MASK : SCRIPT_STANDARD_ID_MAX);
    if (term->shift >= sizeof(uint32_t) * BITS_PER_BYTE || term->mask.value > FRAME_CAN_ID_MASK) {
        return fail(error, term->line_number, "an identifier has 29 bits");
    }
    if ((value & ~mask) != 0) {
        return fail(error, term->line_number, "the value has bits outside the mask");
    }

    pattern->has_id = true;
    pattern->extended = pattern->extended || extended;
    if (!mergeId(pattern, value << term->shift, mask << term->shift)) {
        return fail(error, term->line_number, "the conditions on id contradict each other");
    }
    return true;
}

static bool addByteTerm(const ScriptScope *scope, const ScriptTerm *term, ScriptPattern *pattern, ScriptError *error)
{
    uint32_t value;
    uint32_t mask = term->has_mask ? (uint32_t)term->mask.value : BYTE_MASK;
    uint8_t shift = (uint8_t)(term->first * BITS_PER_BYTE + term->shift);

    if (term->first >= TXN_PATTERN_PAYLOAD_BYTES) {
        return fail(error, term->line_number, "only byte[0] to byte[7] can be compared");
    }
    if (mask > BYTE_MASK || term->shift >= BITS_PER_BYTE) {
        return fail(error, term->line_number, "a byte has 8 bits");
    }
    if (!ScriptResolver_Value(scope, &term->value, &value, NULL, error)) {
        return false;
    }
    if ((value & ~mask) != 0) {
        return fail(error, term->line_number, "the value has bits outside the mask");
    }
    if (!mergePayload(pattern, (uint64_t)value << shift, (uint64_t)mask << shift)) {
        return fail(error, term->line_number, "the conditions on this byte contradict each other");
    }
    return true;
}

static bool addBytesTerm(const ScriptScope *scope, const ScriptTerm *term, ScriptPattern *pattern, ScriptError *error)
{
    uint8_t count = (uint8_t)(term->last - term->first + 1);
    uint64_t value = 0;
    uint64_t mask = 0;
    uint32_t number;
    uint8_t byte;
    uint8_t i;

    if (term->last >= TXN_PATTERN_PAYLOAD_BYTES) {
        return fail(error, term->line_number, "only byte[0] to byte[7] can be compared");
    }
    if (count > CHECK_BYTES_MAX) {
        return fail(error, term->line_number, "a value spans at most 4 bytes");
    }
    if (!checkValue(scope, term, &number, error)) {
        return false;
    }
    if (count < CHECK_BYTES_MAX && (number >> (count * BITS_PER_BYTE)) != 0) {
        return fail(error, term->line_number, "the value does not fit those bytes");
    }

    for(i=0; i<count; i++) {
        byte = term->check.order == kSCRIPT_BYTE_ORDER_LITTLE_ENDIAN ? i : (uint8_t)(count - 1 - i);
        value |= (uint64_t)((number >> (byte * BITS_PER_BYTE)) & BYTE_MASK) << ((term->first + i) * BITS_PER_BYTE);
        mask |= (uint64_t)BYTE_MASK << ((term->first + i) * BITS_PER_BYTE);
    }
    if (!mergePayload(pattern, value, mask)) {
        return fail(error, term->line_number, "the conditions on these bytes contradict each other");
    }
    return true;
}

static bool checkValue(const ScriptScope *scope, const ScriptTerm *term, uint32_t *value, ScriptError *error)
{
    const ScriptCheck *check = &term->check;
    ChecksumModel model;

    if (check->function.length == 0) {
        if (check->literal.value > UINT32_MAX) {
            return fail(error, term->line_number, "the value does not fit 4 bytes");
        }
        *value = (uint32_t)check->literal.value;
        return true;
    }
    if (!ScriptResolver_CheckModel(scope->script, &check->function, &model)) {
        return fail(error, term->line_number, "unknown check: sum8, sum16, sum32, crc16, crc32 or a crc() constant");
    }
    if (checkBytes(&model) != term->last - term->first + 1) {
        return fail(error, term->line_number, "the check's width does not match the number of bytes");
    }
    if (!checkOverData(scope, &check->data, &model, value)) {
        return fail(error, term->line_number, "the check takes a file constant or a chunk");
    }
    return true;
}

static bool checkOverData(const ScriptScope *scope, const ScriptName *data, const ChecksumModel *model, uint32_t *value)
{
    const ScriptBinding *binding = ScriptResolver_Binding(scope->bindings, data);
    const ScriptConstant *constant = ScriptResolver_Constant(scope->script, data);
    Checksum checksum;

    *value = 0;
    if (binding != NULL) {
        if (binding->kind != kSCRIPT_BINDING_CHUNK) {
            return false;
        }
        if (binding->data != NULL) {
            Checksum_Start(&checksum, model);
            Checksum_Update(&checksum, binding->data, binding->size);
            *value = Checksum_Finish(&checksum);
        }
        return true;
    }
    if (constant == NULL || constant->kind != kSCRIPT_CONSTANT_FILE) {
        return false;
    }
    if (scope->files == NULL) {
        return true;
    }
    return checkOverFile(scope, constant, model, value);
}

static bool checkOverFile(const ScriptScope *scope, const ScriptConstant *file, const ChecksumModel *model, uint32_t *value)
{
    uint8_t buffer[FILE_READ_SIZE];
    uint64_t offset = 0;
    uint32_t read_size;
    Checksum checksum;

    Checksum_Start(&checksum, model);
    for (;;) {
        if (!scope->files->read(scope->files->context, file->path, offset, buffer, sizeof(buffer), &read_size)) {
            return false;
        }
        if (read_size == 0) {
            break;
        }
        Checksum_Update(&checksum, buffer, read_size);
        offset += read_size;
    }

    *value = Checksum_Finish(&checksum);
    return true;
}

static bool mergeId(ScriptPattern *pattern, uint32_t value, uint32_t mask)
{
    uint32_t shared = pattern->can_mask & mask;

    if ((pattern->can_id & shared) != (value & shared)) {
        return false;
    }
    pattern->can_mask |= mask;
    pattern->can_id |= value & mask;
    return true;
}

static bool mergePayload(ScriptPattern *pattern, uint64_t value, uint64_t mask)
{
    uint64_t shared = pattern->payload_mask & mask;

    if ((pattern->payload_value & shared) != (value & shared)) {
        return false;
    }
    pattern->payload_mask |= mask;
    pattern->payload_value |= value & mask;
    return true;
}

static uint8_t checkBytes(const ChecksumModel *model)
{
    return (uint8_t)((model->width + BITS_PER_BYTE - 1) / BITS_PER_BYTE);
}
