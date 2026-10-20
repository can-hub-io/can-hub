#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "client/txn_script/script.h"
#include "client/txn_script/script_error.h"

/*
 * Builds the syntax tree of a transaction script. It checks the shape of every
 * line and the indentation of the blocks; names, limits and what the agent
 * can compare are the compiler's to check. On failure error carries the line
 * and the reason.
 */
bool ScriptParser_Parse(Script *script, const char *text, size_t size, ScriptError *error);
