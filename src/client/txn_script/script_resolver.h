#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "client/txn_script/script.h"
#include "client/txn_script/script_error.h"

#define SCRIPT_BINDINGS_MAX 8
#define SCRIPT_STANDARD_ID_MAX 0x7FF
#define SCRIPT_EXTENDED_HEX_DIGITS 8

/*
 * What the compiler and the generator share: name lookup, the loop variables
 * bound at a point of the script, values, and conditions turned into the id
 * and payload patterns the agent compares. A chunk binding without data (the
 * compiler's walk) lets a condition be checked for shape without computing
 * the checks over data; files are read through the platform's port.
 */
typedef enum tscript_binding_e {
    kSCRIPT_BINDING_VALUE = 0,
    kSCRIPT_BINDING_CHUNK = 1,
    kSCRIPT_BINDING_MAX,
} TSCRIPT_BINDING;

typedef struct {
    ScriptName name;
    uint8_t kind;
    uint32_t value;
    const uint8_t *data;
    uint32_t size;
} ScriptBinding;

typedef struct {
    ScriptBinding items[SCRIPT_BINDINGS_MAX];
    uint8_t count;
} ScriptBindings;

typedef struct {
    void *context;
    bool (*read)(void *context, const char *path, uint64_t offset, uint8_t *buffer, uint32_t size, uint32_t *read_size);
} ScriptFiles;

typedef struct {
    bool has_id;
    bool extended;
    uint32_t can_id;
    uint32_t can_mask;
    uint64_t payload_value;
    uint64_t payload_mask;
} ScriptPattern;

typedef struct {
    const Script *script;
    const ScriptBindings *bindings;
    const ScriptFiles *files;
    const ScriptName *roster_variable;
} ScriptScope;

const ScriptConstant *ScriptResolver_Constant(const Script *script, const ScriptName *name);
const ScriptBinding *ScriptResolver_Binding(const ScriptBindings *bindings, const ScriptName *name);
bool ScriptResolver_Value(
    const ScriptScope *scope,
    const ScriptValue *value,
    uint32_t *resolved,
    bool *extended,
    ScriptError *error
);
bool ScriptResolver_Condition(
    const ScriptScope *scope,
    const ScriptCondition *condition,
    ScriptPattern *pattern,
    const ScriptTerm **roster_term,
    ScriptError *error
);
bool ScriptResolver_CheckModel(const Script *script, const ScriptName *function, ChecksumModel *model);
