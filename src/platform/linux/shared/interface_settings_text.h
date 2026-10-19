#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "protocol/ifconfig_message.h"

#define INTERFACE_SETTINGS_TEXT_ERROR_SIZE 96

/*
 * The ip-link style text form of interface settings, shared by the cli and the
 * client: "bitrate 500000 sample-point 0.875 fd on dbitrate 2000000 up", or the
 * raw form "tq 125 prop-seg 6 phase-seg1 7 phase-seg2 2 sjw 1" (d-prefixed for
 * the data phase), plus "termination 120". Each word names one field, so the
 * parsed settings carry exactly those fields.
 * Print writes one "key value" line per field the settings carry.
 */
bool InterfaceSettingsText_Parse(
    InterfaceSettings *settings,
    const char *const *words,
    uint8_t word_count,
    char *error
);
void InterfaceSettingsText_Print(FILE *stream, const InterfaceSettings *settings);
