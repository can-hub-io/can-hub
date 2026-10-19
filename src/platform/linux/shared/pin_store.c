#include "platform/linux/shared/pin_store.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sys/stat.h>

#define LINE_BUFFER_SIZE 2048
#define PIN_FILE_MODE 0600
#define FINGERPRINT_HEX_LENGTH (PIN_STORE_FINGERPRINT_HEX_SIZE - 1)
#define KEY_SEPARATOR ','
#define REWRITE_SUFFIX ".tmp"
#define REWRITE_PATH_MAX 1024
#define ENTRY_PUNCTUATION_LENGTH 3

typedef struct {
    char keys[LINE_BUFFER_SIZE];
    char fingerprint[PIN_STORE_FINGERPRINT_HEX_SIZE];
} PinEntry;

static char lowercase(char character);
static bool isHexDigit(char character);
static bool readEntry(FILE *file, char *line, PinEntry *entry, bool *parsed);
static bool hasKey(const char *keys, const char *key);
static bool appendEntry(const char *path, const char *key, const char *fingerprint_hex);
static bool addKeyToEntry(const char *path, const char *key, const char *fingerprint_hex);
static bool copyWithKeyAdded(FILE *source, FILE *target, const char *key, const char *fingerprint_hex);
static bool replaceFile(const char *path, const char *replacement);

/* ---------- public ---------- */

void PinStore_HubKey(const char *host, const char *port_text, char *key, size_t key_size)
{
    size_t host_length = strlen(host);
    size_t position = 0;
    size_t i;

    while (host_length > 0 && host[host_length - 1] == '.') {
        host_length--;
    }

    for(i=0; i<host_length && position + 1 < key_size; i++) {
        key[position++] = lowercase(host[i]);
    }
    key[position] = '\0';

    snprintf(key + position, key_size - position, ":%s", port_text);
}

bool PinStore_ParseFingerprint(const char *text, char *fingerprint_hex)
{
    size_t i;

    if (strlen(text) != FINGERPRINT_HEX_LENGTH) {
        return false;
    }

    for(i=0; i<FINGERPRINT_HEX_LENGTH; i++) {
        if (!isHexDigit(text[i])) {
            return false;
        }
        fingerprint_hex[i] = lowercase(text[i]);
    }
    fingerprint_hex[FINGERPRINT_HEX_LENGTH] = '\0';

    return true;
}

bool PinStore_Lookup(const char *path, const char *key, char *fingerprint_hex)
{
    char line[LINE_BUFFER_SIZE];
    PinEntry entry;
    FILE *file = fopen(path, "r");
    bool parsed;
    bool found = false;

    if (file == NULL) {
        return false;
    }

    while (!found && readEntry(file, line, &entry, &parsed)) {
        if (parsed && hasKey(entry.keys, key)) {
            snprintf(fingerprint_hex, PIN_STORE_FINGERPRINT_HEX_SIZE, "%s", entry.fingerprint);
            found = true;
        }
    }

    fclose(file);

    return found;
}

bool PinStore_KnowsFingerprint(const char *path, const char *fingerprint_hex)
{
    char line[LINE_BUFFER_SIZE];
    PinEntry entry;
    FILE *file = fopen(path, "r");
    bool parsed;
    bool found = false;

    if (file == NULL) {
        return false;
    }

    while (!found && readEntry(file, line, &entry, &parsed)) {
        found = parsed && strcmp(entry.fingerprint, fingerprint_hex) == 0;
    }

    fclose(file);

    return found;
}

bool PinStore_IsEmpty(const char *path)
{
    char line[LINE_BUFFER_SIZE];
    PinEntry entry;
    FILE *file = fopen(path, "r");
    bool parsed;
    bool empty = true;

    if (file == NULL) {
        return true;
    }

    while (empty && readEntry(file, line, &entry, &parsed)) {
        empty = !parsed;
    }

    fclose(file);

    return empty;
}

bool PinStore_Pin(const char *path, const char *key, const char *fingerprint_hex)
{
    if (PinStore_KnowsFingerprint(path, fingerprint_hex) && addKeyToEntry(path, key, fingerprint_hex)) {
        return true;
    }

    return appendEntry(path, key, fingerprint_hex);
}

/* ---------- private ---------- */

static char lowercase(char character)
{
    if (character >= 'A' && character <= 'Z') {
        return (char)(character - 'A' + 'a');
    }

    return character;
}

static bool isHexDigit(char character)
{
    char folded = lowercase(character);

    return (folded >= '0' && folded <= '9') || (folded >= 'a' && folded <= 'f');
}

static bool readEntry(FILE *file, char *line, PinEntry *entry, bool *parsed)
{
    if (fgets(line, LINE_BUFFER_SIZE, file) == NULL) {
        return false;
    }

    *parsed = sscanf(line, "%2047s %64s", entry->keys, entry->fingerprint) == 2;

    return true;
}

static bool hasKey(const char *keys, const char *key)
{
    size_t key_length = strlen(key);
    const char *segment = keys;
    const char *separator;
    size_t segment_length;

    while (segment != NULL) {
        separator = strchr(segment, KEY_SEPARATOR);
        segment_length = (separator != NULL) ? (size_t)(separator - segment) : strlen(segment);
        if (segment_length == key_length && memcmp(segment, key, key_length) == 0) {
            return true;
        }
        segment = (separator != NULL) ? separator + 1 : NULL;
    }

    return false;
}

static bool appendEntry(const char *path, const char *key, const char *fingerprint_hex)
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

static bool addKeyToEntry(const char *path, const char *key, const char *fingerprint_hex)
{
    char replacement[REWRITE_PATH_MAX];
    FILE *source;
    FILE *target;
    bool added;

    snprintf(replacement, sizeof(replacement), "%s%s", path, REWRITE_SUFFIX);
    source = fopen(path, "r");
    if (source == NULL) {
        return false;
    }
    target = fopen(replacement, "w");
    if (target == NULL) {
        fclose(source);
        return false;
    }
    chmod(replacement, PIN_FILE_MODE);

    added = copyWithKeyAdded(source, target, key, fingerprint_hex);
    fclose(source);
    if (fclose(target) != 0 || !added) {
        remove(replacement);
        return false;
    }

    return replaceFile(path, replacement);
}

static bool copyWithKeyAdded(FILE *source, FILE *target, const char *key, const char *fingerprint_hex)
{
    char line[LINE_BUFFER_SIZE];
    PinEntry entry;
    bool parsed;
    bool added = false;

    while (readEntry(source, line, &entry, &parsed)) {
        if (!added && parsed && strcmp(entry.fingerprint, fingerprint_hex) == 0
            && strlen(entry.keys) + strlen(key) + FINGERPRINT_HEX_LENGTH + ENTRY_PUNCTUATION_LENGTH < LINE_BUFFER_SIZE) {
            fprintf(target, "%s%c%s %s\n", entry.keys, KEY_SEPARATOR, key, entry.fingerprint);
            added = true;
            continue;
        }
        fputs(line, target);
    }

    return added;
}

static bool replaceFile(const char *path, const char *replacement)
{
    if (rename(replacement, path) == 0) {
        return true;
    }

    remove(path);
    return rename(replacement, path) == 0;
}
