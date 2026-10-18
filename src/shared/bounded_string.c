#include "shared/bounded_string.h"

#include <string.h>

void BoundedString_Copy(char *destination, size_t destination_size, const char *source)
{
    size_t length;

    if (destination_size == 0) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }

    length = strnlen(source, destination_size - 1);
    memcpy(destination, source, length);
    destination[length] = '\0';
}
