#include "platform/linux/shared/log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int32_t syslogPriority(LogLevel level);
static const char *levelTag(LogLevel level);

static LogLevel current_level = kLOG_LEVEL_INFO;
static bool priority_prefix = false;
static const char *component_name = "can-hub";

void Log_Init(const char *component)
{
    const char *level_text;
    LogLevel parsed;

    if (component != NULL) {
        component_name = component;
    }

    priority_prefix = (getenv("JOURNAL_STREAM") != NULL);

    level_text = getenv("CAN_HUB_LOG");
    if (level_text != NULL && Log_ParseLevel(level_text, &parsed)) {
        current_level = parsed;
    }
}

void Log_InitFromArgs(const char *component, int argc, char **argv)
{
    int32_t i;
    LogLevel parsed;

    Log_Init(component);

    for(i=1; i<argc; i++) {
        if (strcmp(argv[i], "--log-level") == 0 && i + 1 < argc && Log_ParseLevel(argv[i + 1], &parsed)) {
            Log_SetLevel(parsed);
        }
    }
}

void Log_SetLevel(LogLevel level)
{
    if (level >= kLOG_LEVEL_MAX) {
        return;
    }

    current_level = level;
}

bool Log_ParseLevel(const char *text, LogLevel *level)
{
    if (text == NULL || level == NULL) {
        return false;
    }

    if (strcmp(text, "error") == 0) {
        *level = kLOG_LEVEL_ERROR;
        return true;
    }
    if (strcmp(text, "warn") == 0) {
        *level = kLOG_LEVEL_WARN;
        return true;
    }
    if (strcmp(text, "info") == 0) {
        *level = kLOG_LEVEL_INFO;
        return true;
    }
    if (strcmp(text, "debug") == 0) {
        *level = kLOG_LEVEL_DEBUG;
        return true;
    }

    return false;
}

bool Log_IsEnabled(LogLevel level)
{
    return level <= current_level;
}

void Log_Message(LogLevel level, const char *format, ...)
{
    char message[1024];
    char line[1152];
    va_list args;

    if (!Log_IsEnabled(level)) {
        return;
    }

    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    Log_Format(line, sizeof(line), priority_prefix, level, component_name, message);
    fputs(line, stderr);
}

int32_t Log_Format(char *destination, size_t capacity, bool emit_priority, LogLevel level, const char *component, const char *message)
{
    if (destination == NULL || capacity == 0) {
        return -1;
    }

    if (emit_priority) {
        return snprintf(destination, capacity, "<%d>%s: %s%s\n", (int)syslogPriority(level), component, levelTag(level), message);
    }

    return snprintf(destination, capacity, "%s: %s%s\n", component, levelTag(level), message);
}

static int32_t syslogPriority(LogLevel level)
{
    switch (level) {
        case kLOG_LEVEL_ERROR:
            return 3;
        case kLOG_LEVEL_WARN:
            return 4;
        case kLOG_LEVEL_INFO:
            return 6;
        case kLOG_LEVEL_DEBUG:
            return 7;
        default:
            return 6;
    }
}

static const char *levelTag(LogLevel level)
{
    switch (level) {
        case kLOG_LEVEL_ERROR:
            return "error: ";
        case kLOG_LEVEL_WARN:
            return "warning: ";
        case kLOG_LEVEL_DEBUG:
            return "debug: ";
        default:
            return "";
    }
}
