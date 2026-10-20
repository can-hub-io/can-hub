#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "protocol/register_message.h"

/*
 * Outbound contract for the TOFU agent identity store: the broker pins the
 * TLS fingerprint of an agent name on first registration and rejects later
 * registrations whose fingerprint differs. The admin plane enrolls, lists and
 * forgets pins through the same port; only an enrolled fingerprint is known,
 * since anyone can be pinned on first contact. The platform decides where the
 * pins live (SQLite on the hub).
 */

#define IDENTITY_FINGERPRINT_HEX_SIZE 65

typedef struct {
    char agent_name[REGISTER_AGENT_NAME_SIZE];
    char fingerprint_hex[IDENTITY_FINGERPRINT_HEX_SIZE];
} IdentityPin;

typedef struct {
    void *context;
    bool (*lookup)(void *context, const char *agent_name, char *fingerprint_hex);
    bool (*knows)(void *context, const char *fingerprint_hex);
    uint32_t (*count_first_contact)(void *context);
    bool (*pin)(void *context, const char *agent_name, const char *fingerprint_hex);
    bool (*enroll)(void *context, const char *agent_name, const char *fingerprint_hex);
    bool (*forget)(void *context, const char *agent_name);
    uint8_t (*list)(void *context, uint16_t offset, IdentityPin *pins, uint8_t max_pins, bool *more);
} IdentityStorePort;
