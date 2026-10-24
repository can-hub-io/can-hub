#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "client/txn_script/script_compiler.h"
#include "client/txn_script/script_resolver.h"

/*
 * Produces the steps of a compiled script one at a time, in order, as the
 * libcanhub transaction API takes them, unrolling its loops: list loops value
 * by value, chunk loops reading their file through the platform's port as
 * they go, so a firmware image never sits whole in memory. A step's data and
 * where live in the generator until the next step; line_number is the line
 * the last step came from.
 */
typedef enum tscript_generated_e {
    kSCRIPT_GENERATED_STEP = 0,
    kSCRIPT_GENERATED_END,
    kSCRIPT_GENERATED_ERROR,
    kSCRIPT_GENERATED_MAX,
} TSCRIPT_GENERATED;

typedef struct {
    uint16_t statement;
    uint16_t cursor;
    uint8_t range;
    uint32_t value;
    uint32_t chunk_index;
    uint32_t chunk_size;
    uint8_t buffer[SCRIPT_CHUNK_SIZE_MAX];
} ScriptGeneratorFrame;

typedef struct {
    const ScriptProgram *program;
    ScriptFiles files;
    ScriptGeneratorFrame frames[SCRIPT_BINDINGS_MAX];
    uint8_t depth;
    uint16_t root_cursor;
    ScriptBindings bindings;
    uint8_t data[CANHUB_FRAME_PAYLOAD_MAX];
    CanHubTxnMatch where;
    uint16_t line_number;
} ScriptGenerator;

void ScriptGenerator_Start(ScriptGenerator *self, const ScriptProgram *program, const ScriptFiles *files);
uint8_t ScriptGenerator_Next(ScriptGenerator *self, CanHubTxnStep *step, ScriptError *error);
bool ScriptGenerator_Keepalive(
    ScriptGenerator *self,
    const ScriptStatement *statement,
    CanHubTxnKeepalive *keepalive,
    ScriptError *error
);
