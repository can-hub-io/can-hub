#include "platform/linux/shared/interface_settings_text.h"

#include <string.h>

#define SAMPLE_POINT_DIGITS_MAX 3
#define SAMPLE_POINT_SCALE 1000
#define DECIMAL_BASE 10
#define U32_DECIMAL_DIGITS_MAX 10
#define KEY_COLUMN_WIDTH 16
#define DATA_PHASE_PREFIX 'd'
#define TERMINATION_KEY "termination"

typedef struct {
    const char *name;
    uint32_t flag;
} CtrlmodeName;

static const CtrlmodeName CTRLMODE_NAMES[] = {
    { "fd", INTERFACE_CTRLMODE_FD },
    { "listen-only", INTERFACE_CTRLMODE_LISTEN_ONLY },
    { "loopback", INTERFACE_CTRLMODE_LOOPBACK },
    { "one-shot", INTERFACE_CTRLMODE_ONE_SHOT },
    { "triple-sampling", INTERFACE_CTRLMODE_TRIPLE_SAMPLING },
    { "berr-reporting", INTERFACE_CTRLMODE_BERR_REPORTING },
};

static const char *const STATE_NAMES[] = {
    "error-active",
    "error-warning",
    "error-passive",
    "bus-off",
    "stopped",
    "sleeping",
};

static bool parseWord(InterfaceSettings *settings, const char *key, const char *value, char *error);
static bool parseNumber(InterfaceSettings *settings, const char *key, const char *value, char *error);
static bool parseTiming(uint32_t *member, uint32_t field, InterfaceSettings *settings, const char *key, const char *value, char *error);
static bool parseTermination(InterfaceSettings *settings, const char *value, char *error);
static uint32_t *timingMember(InterfaceBitTiming *timing, const char *name);
static bool parseCtrlmode(InterfaceSettings *settings, uint32_t flag, const char *key, const char *value, char *error);
static const CtrlmodeName *findCtrlmode(const char *name);
static bool isLinkWord(const char *word);
static bool readU32(const char *text, uint32_t *value);
static bool readSamplePoint(const char *text, uint16_t *value);
static void printSamplePoint(FILE *stream, const char *key, uint16_t sample_point);
static void printCtrlmodes(FILE *stream, const InterfaceSettings *settings);
static void printBitTiming(FILE *stream, const char *prefix, const InterfaceBitTiming *timing);
static const char *stateName(uint8_t can_state);

/* ---------- public ---------- */

bool InterfaceSettingsText_Parse(
    InterfaceSettings *settings,
    const char *const *words,
    uint8_t word_count,
    char *error
)
{
    uint8_t i = 0;

    memset(settings, 0, sizeof(*settings));
    error[0] = '\0';

    if (word_count == 0) {
        snprintf(error, INTERFACE_SETTINGS_TEXT_ERROR_SIZE, "nothing to set");
        return false;
    }

    while (i < word_count) {
        if (isLinkWord(words[i])) {
            settings->fields |= INTERFACE_FIELD_LINK;
            settings->link_up = strcmp(words[i], "up") == 0;
            i++;
            continue;
        }
        if (i + 1 >= word_count) {
            snprintf(error, INTERFACE_SETTINGS_TEXT_ERROR_SIZE, "%s needs a value", words[i]);
            return false;
        }
        if (!parseWord(settings, words[i], words[i + 1], error)) {
            return false;
        }
        i += 2;
    }

    return true;
}

void InterfaceSettingsText_Print(FILE *stream, const InterfaceSettings *settings)
{
    if (settings->fields & INTERFACE_FIELD_LINK) {
        fprintf(stream, "%-*s %s\n", KEY_COLUMN_WIDTH, "link", settings->link_up ? "up" : "down");
    }
    if (settings->fields & INTERFACE_FIELD_STATE) {
        fprintf(stream, "%-*s %s\n", KEY_COLUMN_WIDTH, "state", stateName(settings->can_state));
    }
    if (settings->fields & INTERFACE_FIELD_BITRATE) {
        fprintf(stream, "%-*s %u\n", KEY_COLUMN_WIDTH, "bitrate", settings->bitrate);
    }
    if (settings->fields & INTERFACE_FIELD_SAMPLE_POINT) {
        printSamplePoint(stream, "sample-point", settings->sample_point);
    }
    if (settings->fields & INTERFACE_FIELD_BIT_TIMING) {
        printBitTiming(stream, "", &settings->bit_timing);
    }
    if (settings->fields & INTERFACE_FIELD_DATA_BITRATE) {
        fprintf(stream, "%-*s %u\n", KEY_COLUMN_WIDTH, "dbitrate", settings->data_bitrate);
    }
    if (settings->fields & INTERFACE_FIELD_DATA_SAMPLE_POINT) {
        printSamplePoint(stream, "dsample-point", settings->data_sample_point);
    }
    if (settings->fields & INTERFACE_FIELD_DATA_BIT_TIMING) {
        printBitTiming(stream, "d", &settings->data_bit_timing);
    }
    if (settings->fields & INTERFACE_FIELD_CTRLMODE) {
        printCtrlmodes(stream, settings);
    }
    if (settings->fields & INTERFACE_FIELD_RESTART_MS) {
        fprintf(stream, "%-*s %u\n", KEY_COLUMN_WIDTH, "restart-ms", settings->restart_ms);
    }
    if (settings->fields & INTERFACE_FIELD_TXQUEUELEN) {
        fprintf(stream, "%-*s %u\n", KEY_COLUMN_WIDTH, "txqueuelen", settings->txqueuelen);
    }
    if (settings->fields & INTERFACE_FIELD_TERMINATION) {
        fprintf(stream, "%-*s %u\n", KEY_COLUMN_WIDTH, TERMINATION_KEY, settings->termination);
    }
}

/* ---------- private ---------- */

static bool parseWord(InterfaceSettings *settings, const char *key, const char *value, char *error)
{
    const CtrlmodeName *ctrlmode = findCtrlmode(key);
    uint32_t *nominal_member = timingMember(&settings->bit_timing, key);
    uint32_t *data_member = NULL;

    if (key[0] == DATA_PHASE_PREFIX) {
        data_member = timingMember(&settings->data_bit_timing, key + 1);
    }

    if (ctrlmode != NULL) {
        return parseCtrlmode(settings, ctrlmode->flag, key, value, error);
    }
    if (nominal_member != NULL) {
        return parseTiming(nominal_member, INTERFACE_FIELD_BIT_TIMING, settings, key, value, error);
    }
    if (data_member != NULL) {
        return parseTiming(data_member, INTERFACE_FIELD_DATA_BIT_TIMING, settings, key, value, error);
    }
    if (strcmp(key, TERMINATION_KEY) == 0) {
        return parseTermination(settings, value, error);
    }

    return parseNumber(settings, key, value, error);
}

static bool parseNumber(InterfaceSettings *settings, const char *key, const char *value, char *error)
{
    bool parsed = false;

    if (strcmp(key, "bitrate") == 0) {
        settings->fields |= INTERFACE_FIELD_BITRATE;
        parsed = readU32(value, &settings->bitrate) && settings->bitrate != 0;
    } else if (strcmp(key, "dbitrate") == 0) {
        settings->fields |= INTERFACE_FIELD_DATA_BITRATE;
        parsed = readU32(value, &settings->data_bitrate) && settings->data_bitrate != 0;
    } else if (strcmp(key, "sample-point") == 0) {
        settings->fields |= INTERFACE_FIELD_SAMPLE_POINT;
        parsed = readSamplePoint(value, &settings->sample_point);
    } else if (strcmp(key, "dsample-point") == 0) {
        settings->fields |= INTERFACE_FIELD_DATA_SAMPLE_POINT;
        parsed = readSamplePoint(value, &settings->data_sample_point);
    } else if (strcmp(key, "restart-ms") == 0) {
        settings->fields |= INTERFACE_FIELD_RESTART_MS;
        parsed = readU32(value, &settings->restart_ms);
    } else if (strcmp(key, "txqueuelen") == 0) {
        settings->fields |= INTERFACE_FIELD_TXQUEUELEN;
        parsed = readU32(value, &settings->txqueuelen);
    } else {
        snprintf(error, INTERFACE_SETTINGS_TEXT_ERROR_SIZE, "unknown setting %s", key);
        return false;
    }

    if (!parsed) {
        snprintf(error, INTERFACE_SETTINGS_TEXT_ERROR_SIZE, "invalid %s %s", key, value);
    }

    return parsed;
}

static bool parseTiming(uint32_t *member, uint32_t field, InterfaceSettings *settings, const char *key, const char *value, char *error)
{
    settings->fields |= field;
    if (readU32(value, member) && *member != 0) {
        return true;
    }

    snprintf(error, INTERFACE_SETTINGS_TEXT_ERROR_SIZE, "invalid %s %s", key, value);

    return false;
}

static bool parseTermination(InterfaceSettings *settings, const char *value, char *error)
{
    uint32_t ohms;

    if (!readU32(value, &ohms) || ohms > UINT16_MAX) {
        snprintf(error, INTERFACE_SETTINGS_TEXT_ERROR_SIZE, "invalid %s %s", TERMINATION_KEY, value);
        return false;
    }

    settings->fields |= INTERFACE_FIELD_TERMINATION;
    settings->termination = (uint16_t)ohms;

    return true;
}

static uint32_t *timingMember(InterfaceBitTiming *timing, const char *name)
{
    if (strcmp(name, "tq") == 0) {
        return &timing->tq;
    }
    if (strcmp(name, "prop-seg") == 0) {
        return &timing->prop_seg;
    }
    if (strcmp(name, "phase-seg1") == 0) {
        return &timing->phase_seg1;
    }
    if (strcmp(name, "phase-seg2") == 0) {
        return &timing->phase_seg2;
    }
    if (strcmp(name, "sjw") == 0) {
        return &timing->sjw;
    }

    return NULL;
}

static bool parseCtrlmode(InterfaceSettings *settings, uint32_t flag, const char *key, const char *value, char *error)
{
    if (strcmp(value, "on") != 0 && strcmp(value, "off") != 0) {
        snprintf(error, INTERFACE_SETTINGS_TEXT_ERROR_SIZE, "%s takes on or off", key);
        return false;
    }

    settings->fields |= INTERFACE_FIELD_CTRLMODE;
    settings->ctrlmode_mask |= flag;
    if (strcmp(value, "on") == 0) {
        settings->ctrlmode_flags |= flag;
    }

    return true;
}

static const CtrlmodeName *findCtrlmode(const char *name)
{
    uint8_t i;

    for(i=0; i<sizeof(CTRLMODE_NAMES) / sizeof(CTRLMODE_NAMES[0]); i++) {
        if (strcmp(CTRLMODE_NAMES[i].name, name) == 0) {
            return &CTRLMODE_NAMES[i];
        }
    }

    return NULL;
}

static bool isLinkWord(const char *word)
{
    return strcmp(word, "up") == 0 || strcmp(word, "down") == 0;
}

static bool readU32(const char *text, uint32_t *value)
{
    uint64_t accumulated = 0;
    uint8_t digits = 0;

    while (text[digits] >= '0' && text[digits] <= '9') {
        accumulated = accumulated * DECIMAL_BASE + (uint64_t)(text[digits] - '0');
        digits++;
        if (digits > U32_DECIMAL_DIGITS_MAX || accumulated > UINT32_MAX) {
            return false;
        }
    }

    if (digits == 0 || text[digits] != '\0') {
        return false;
    }

    *value = (uint32_t)accumulated;

    return true;
}

static bool readSamplePoint(const char *text, uint16_t *value)
{
    uint16_t scaled = 0;
    uint16_t scale = SAMPLE_POINT_SCALE;
    uint8_t digits = 0;

    if (text[0] != '0' || text[1] != '.') {
        return false;
    }

    while (text[2 + digits] >= '0' && text[2 + digits] <= '9') {
        if (digits == SAMPLE_POINT_DIGITS_MAX) {
            return false;
        }
        scale /= DECIMAL_BASE;
        scaled = (uint16_t)(scaled + (text[2 + digits] - '0') * scale);
        digits++;
    }

    if (digits == 0 || text[2 + digits] != '\0') {
        return false;
    }

    *value = scaled;

    return true;
}

static void printSamplePoint(FILE *stream, const char *key, uint16_t sample_point)
{
    fprintf(stream, "%-*s %u.%03u\n", KEY_COLUMN_WIDTH, key, sample_point / SAMPLE_POINT_SCALE, sample_point % SAMPLE_POINT_SCALE);
}

static void printCtrlmodes(FILE *stream, const InterfaceSettings *settings)
{
    uint8_t i;

    for(i=0; i<sizeof(CTRLMODE_NAMES) / sizeof(CTRLMODE_NAMES[0]); i++) {
        if ((settings->ctrlmode_mask & CTRLMODE_NAMES[i].flag) == 0) {
            continue;
        }
        fprintf(
            stream,
            "%-*s %s\n",
            KEY_COLUMN_WIDTH,
            CTRLMODE_NAMES[i].name,
            (settings->ctrlmode_flags & CTRLMODE_NAMES[i].flag) ? "on" : "off"
        );
    }
}

static void printBitTiming(FILE *stream, const char *prefix, const InterfaceBitTiming *timing)
{
    char key[KEY_COLUMN_WIDTH];

    snprintf(key, sizeof(key), "%stq", prefix);
    fprintf(stream, "%-*s %u\n", KEY_COLUMN_WIDTH, key, timing->tq);
    snprintf(key, sizeof(key), "%sprop-seg", prefix);
    fprintf(stream, "%-*s %u\n", KEY_COLUMN_WIDTH, key, timing->prop_seg);
    snprintf(key, sizeof(key), "%sphase-seg1", prefix);
    fprintf(stream, "%-*s %u\n", KEY_COLUMN_WIDTH, key, timing->phase_seg1);
    snprintf(key, sizeof(key), "%sphase-seg2", prefix);
    fprintf(stream, "%-*s %u\n", KEY_COLUMN_WIDTH, key, timing->phase_seg2);
    snprintf(key, sizeof(key), "%ssjw", prefix);
    fprintf(stream, "%-*s %u\n", KEY_COLUMN_WIDTH, key, timing->sjw);
    snprintf(key, sizeof(key), "%sbrp", prefix);
    fprintf(stream, "%-*s %u\n", KEY_COLUMN_WIDTH, key, timing->brp);
}

static const char *stateName(uint8_t can_state)
{
    if (can_state >= sizeof(STATE_NAMES) / sizeof(STATE_NAMES[0])) {
        return "unknown";
    }

    return STATE_NAMES[can_state];
}
