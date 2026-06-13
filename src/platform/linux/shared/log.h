#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum log_level_e {
    kLOG_LEVEL_ERROR,
    kLOG_LEVEL_WARN,
    kLOG_LEVEL_INFO,
    kLOG_LEVEL_DEBUG,
    kLOG_LEVEL_MAX
} LogLevel;

void Log_Init(const char *component);
void Log_InitFromArgs(const char *component, int argc, char **argv);
void Log_SetLevel(LogLevel level);
bool Log_ParseLevel(const char *text, LogLevel *level);
bool Log_IsEnabled(LogLevel level);
void Log_Message(LogLevel level, const char *format, ...) __attribute__((format(printf, 2, 3)));
int32_t Log_Format(char *destination, size_t capacity, bool emit_priority, LogLevel level, const char *component, const char *message);

#define LOG_ERROR(...) Log_Message(kLOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_WARN(...) Log_Message(kLOG_LEVEL_WARN, __VA_ARGS__)
#define LOG_INFO(...) Log_Message(kLOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_DEBUG(...) Log_Message(kLOG_LEVEL_DEBUG, __VA_ARGS__)
