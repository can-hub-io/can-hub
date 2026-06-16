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
 *
 * Push accepts only what fits the buffer and returns the bytes consumed, so an
 * input larger than the buffer (a burst of commands in one read) must be fed in
 * a loop that drains complete commands between pushes; a single command longer
 * than the buffer is the only thing that cannot make progress (Push returns 0).
 */

#define ASCII_FRAMER_BUFFER_SIZE 512

typedef struct {
    uint8_t buffer[ASCII_FRAMER_BUFFER_SIZE];
    size_t used;
} AsciiFramer;

void AsciiFramer_Reset(AsciiFramer *self);
size_t AsciiFramer_Push(AsciiFramer *self, const uint8_t *data, size_t size);
bool AsciiFramer_Next(AsciiFramer *self, char *command, size_t command_size, size_t *command_length);
