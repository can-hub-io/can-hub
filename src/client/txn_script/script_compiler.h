#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "client/txn_script/script.h"
#include "client/txn_script/script_error.h"
#include "client/txn_script/script_resolver.h"
#include "protocol/txn_message.h"

#define SCRIPT_TRANSMIT_ONLY SCRIPT_NO_INDEX
#define SCRIPT_CHUNK_SIZE_MAX 4096

/*
 * Checks a parsed script against what the agent can run and turns it into a
 * transaction: the begin (roster, how to tell who answered, exclusive) and up
 * to 16 wire templates. A script template becomes one wire template per shape
 * it is used with, a variant: plain, with the bytes or the identifier a where
 * adds, with a gap. The compiler walks the script once, unrolling list loops,
 * to know every variant before anything is sent; the generator asks for the
 * shape of each step again and finds its variant.
 */
typedef struct {
    uint16_t base;
    uint32_t gap_us;
    uint64_t where_mask;
    bool has_id_override;
    uint32_t id_value;
    uint32_t id_mask;
} ScriptVariantKey;

typedef struct {
    const Script *script;
    TxnBeginMessage begin;
    ScriptName roster_variable;
    ScriptPattern success[SCRIPT_TEMPLATES_MAX];
    ScriptVariantKey variant_keys[TXN_TEMPLATES_MAX];
    TxnTemplateMessage variants[TXN_TEMPLATES_MAX];
    uint8_t variant_count;
} ScriptProgram;

bool ScriptCompiler_Compile(ScriptProgram *program, const Script *script, ScriptError *error);
bool ScriptCompiler_StepShape(
    const ScriptProgram *program,
    const ScriptStatement *statement,
    const ScriptScope *scope,
    ScriptVariantKey *key,
    ScriptPattern *where,
    ScriptError *error
);
bool ScriptCompiler_FindVariant(const ScriptProgram *program, const ScriptVariantKey *key, uint8_t *template_id);
uint16_t ScriptCompiler_TemplateIndex(const Script *script, const ScriptName *name);
