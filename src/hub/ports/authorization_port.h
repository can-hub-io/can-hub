#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "protocol/register_message.h"
#include "hub/ports/identity_store_port.h"

/*
 * Outbound contract for client authorization (ACLs). Read is open by
 * default; write to an interface requires an explicit grant of the client
 * fingerprint on that interface. Grants are keyed by the stable namespaced
 * name (agent_name + interface_name), not the ephemeral interface id, so
 * they survive reconnects and id reuse. The platform decides where the
 * grants live (SQLite on the hub).
 */

typedef struct {
    char fingerprint_hex[IDENTITY_FINGERPRINT_HEX_SIZE];
    char agent_name[REGISTER_AGENT_NAME_SIZE];
    char interface_name[REGISTER_INTERFACE_NAME_SIZE];
    bool can_write;
} AclEntry;

typedef struct {
    void *context;
    bool (*write_allowed)(
        void *context,
        const char *fingerprint_hex,
        const char *agent_name,
        const char *interface_name
    );
    bool (*grant)(
        void *context,
        const char *fingerprint_hex,
        const char *agent_name,
        const char *interface_name,
        bool can_write
    );
    bool (*revoke)(
        void *context,
        const char *fingerprint_hex,
        const char *agent_name,
        const char *interface_name
    );
    uint8_t (*list)(void *context, uint16_t offset, AclEntry *entries, uint8_t max_entries, bool *more);
} AuthorizationPort;
