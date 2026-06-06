#include "platform/linux/shared/pin_store.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sys/stat.h>

#define LINE_BUFFER_SIZE 512
#define PIN_FILE_MODE 0600

bool PinStore_Lookup(const char *path, const char *key, char *fingerprint_hex)
{
    char line[LINE_BUFFER_SIZE];
    char stored_key[PIN_STORE_KEY_MAX];
    char stored_fingerprint[PIN_STORE_FINGERPRINT_HEX_SIZE];
    FILE *file = fopen(path, "r");
    bool found = false;

    if (file == NULL) {
        return false;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        if (sscanf(line, "%255s %64s", stored_key, stored_fingerprint) != 2) {
            continue;
        }
        if (strcmp(stored_key, key) != 0) {
            continue;
        }

        snprintf(fingerprint_hex, PIN_STORE_FINGERPRINT_HEX_SIZE, "%s", stored_fingerprint);
        found = true;
        break;
    }

    fclose(file);

    return found;
}

bool PinStore_Append(const char *path, const char *key, const char *fingerprint_hex)
{
    FILE *file = fopen(path, "a");
    int32_t written;

    if (file == NULL) {
        return false;
    }

    written = fprintf(file, "%s %s\n", key, fingerprint_hex);
    fclose(file);
    chmod(path, PIN_FILE_MODE);

    return written > 0;
}
