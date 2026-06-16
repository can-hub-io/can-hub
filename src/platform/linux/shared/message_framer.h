#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MESSAGE_FRAMER_BUFFER_SIZE 4096

/*
 * Reassembles length-delimited protocol messages out of any reliable byte
 * stream. Pure buffer logic, no I/O.
 *
 * Push accepts only what fits the buffer and returns the bytes consumed, so a
 * read larger than the buffer (a burst of messages) must be fed in a loop that
 * drains complete messages between pushes; a single message larger than the
 * buffer is the only thing that cannot make progress (Push returns 0).
 */
typedef struct {
    uint8_t buffer[MESSAGE_FRAMER_BUFFER_SIZE];
    size_t used;
} MessageFramer;

/* Drain target for a complete message lifted off the stream. */
typedef struct {
    void *context;
    void (*on_message)(void *context, const uint8_t *message, size_t size);
} MessageSink;

void MessageFramer_Reset(MessageFramer *self);
size_t MessageFramer_Push(MessageFramer *self, const uint8_t *data, size_t size);
size_t MessageFramer_NextMessage(const MessageFramer *self, const uint8_t **message);
void MessageFramer_Consume(MessageFramer *self, size_t size);
void MessageFramer_Drain(MessageFramer *self, const MessageSink *sink);
