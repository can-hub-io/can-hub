#pragma once

#include <stddef.h>

/*
 * Bounded copy of a NUL-terminated string into a fixed-size buffer, for the
 * freestanding cores (no stdio). The copy is truncated to fit and always
 * terminated; a NULL source leaves the destination empty.
 */
void BoundedString_Copy(char *destination, size_t destination_size, const char *source);
