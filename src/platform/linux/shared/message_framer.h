#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MESSAGE_FRAMER_BUFFER_SIZE 4096

/*
 * Reassembles length-delimited protocol messages out of any reliable byte
 * stream. Pure buffer logic, no I/O.
 */
typedef struct {
    uint8_t buffer[MESSAGE_FRAMER_BUFFER_SIZE];
    size_t used;
} MessageFramer;

void MessageFramer_Reset(MessageFramer *self);
bool MessageFramer_Push(MessageFramer *self, const uint8_t *data, size_t size);
size_t MessageFramer_NextMessage(const MessageFramer *self, const uint8_t **message);
void MessageFramer_Consume(MessageFramer *self, size_t size);
