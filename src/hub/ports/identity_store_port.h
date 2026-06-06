#pragma once

#include <stdbool.h>

/*
 * Outbound contract for the TOFU agent identity store: the broker pins the
 * TLS fingerprint of an agent name on first registration and rejects later
 * registrations whose fingerprint differs. The platform decides where the
 * pins live (file today, SQLite later).
 */

#define IDENTITY_FINGERPRINT_HEX_SIZE 65

typedef struct {
    void *context;
    bool (*lookup)(void *context, const char *agent_name, char *fingerprint_hex);
    bool (*pin)(void *context, const char *agent_name, const char *fingerprint_hex);
} IdentityStorePort;
