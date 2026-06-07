#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Splits a socketcand byte stream into commands. Each command is delimited by
 * '<' and '>'; bytes outside a pair (whitespace, newlines) are discarded.
 * Push raw bytes, then drain complete commands one at a time. The inner text
 * (between the brackets, brackets excluded) is handed to the caller; the codec
 * tokenizes it. Freestanding, fixed buffer, no heap.
 */

#define ASCII_FRAMER_BUFFER_SIZE 512

typedef struct {
    uint8_t buffer[ASCII_FRAMER_BUFFER_SIZE];
    size_t used;
    bool overflow;
} AsciiFramer;

void AsciiFramer_Reset(AsciiFramer *self);
bool AsciiFramer_Push(AsciiFramer *self, const uint8_t *data, size_t size);
bool AsciiFramer_Next(AsciiFramer *self, char *command, size_t command_size, size_t *command_length);
