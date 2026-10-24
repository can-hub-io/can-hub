#pragma once

#include <stdint.h>

#define SCRIPT_ERROR_MESSAGE_SIZE 160

typedef struct {
    uint16_t line_number;
    char message[SCRIPT_ERROR_MESSAGE_SIZE];
} ScriptError;

void ScriptError_Set(ScriptError *self, uint16_t line_number, const char *message);
