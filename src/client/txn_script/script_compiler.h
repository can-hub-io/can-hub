#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "canhub.h"
#include "client/txn_script/script.h"
#include "client/txn_script/script_error.h"
#include "client/txn_script/script_resolver.h"

#define SCRIPT_TRANSMIT_ONLY SCRIPT_NO_INDEX
#define SCRIPT_CHUNK_SIZE_MAX 4096
#define SCRIPT_DEFAULT_IDLE_TIMEOUT_MS 5000

/*
 * Checks a parsed script and turns it into what the libcanhub transaction API
 * takes: the options, and one CanHubTxnResponse per response block, the nodes
 * of a response for shared by all of them. The compiler walks the script
 * once, unrolling list loops, so a step that cannot be expressed is refused
 * with its line before anything is sent; the generator then builds the steps
 * against these responses.
 */
typedef struct {
    const Script *script;
    CanHubTxnOptions options;
    uint32_t nodes[CANHUB_TXN_ROSTER_MAX];
    uint32_t node_count;
    CanHubTxnMatch failures[SCRIPT_RESPONSES_MAX];
    CanHubTxnMatch pendings[SCRIPT_RESPONSES_MAX];
    CanHubTxnResponse responses[SCRIPT_RESPONSES_MAX];
} ScriptProgram;

bool ScriptCompiler_Compile(ScriptProgram *program, const Script *script, ScriptError *error);
bool ScriptCompiler_StepShape(
    const ScriptProgram *program,
    const ScriptStatement *statement,
    const ScriptScope *scope,
    uint16_t *response,
    CanHubTxnMatch *where,
    ScriptError *error
);
uint16_t ScriptCompiler_ResponseIndex(const Script *script, const ScriptName *name);
