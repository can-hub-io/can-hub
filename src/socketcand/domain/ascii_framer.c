#include "socketcand/domain/ascii_framer.h"

#include <string.h>

/* ---------- public ---------- */

void AsciiFramer_Reset(AsciiFramer *self)
{
    self->used = 0;
}

size_t AsciiFramer_Push(AsciiFramer *self, const uint8_t *data, size_t size)
{
    size_t available = ASCII_FRAMER_BUFFER_SIZE - self->used;
    size_t taken = size < available ? size : available;
    size_t i;

    for(i=0; i<taken; i++) {
        self->buffer[self->used++] = data[i];
    }

    return taken;
}

bool AsciiFramer_Next(AsciiFramer *self, char *command, size_t command_size, size_t *command_length)
{
    size_t start = 0;
    size_t end;
    size_t inner_length;

    while (start < self->used && self->buffer[start] != '<') {
        start++;
    }
    if (start > 0) {
        memmove(self->buffer, self->buffer + start, self->used - start);
        self->used -= start;
    }
    if (self->used == 0 || self->buffer[0] != '<') {
        return false;
    }

    end = 1;
    while (end < self->used && self->buffer[end] != '>') {
        end++;
    }
    if (end >= self->used) {
        return false;
    }

    inner_length = end - 1;
    if (inner_length >= command_size) {
        inner_length = command_size - 1;
    }
    memcpy(command, self->buffer + 1, inner_length);
    command[inner_length] = '\0';
    *command_length = inner_length;

    memmove(self->buffer, self->buffer + end + 1, self->used - (end + 1));
    self->used -= (end + 1);

    return true;
}
