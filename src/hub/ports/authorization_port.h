#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "protocol/register_message.h"
#include "hub/ports/identity_store_port.h"

/*
 * Outbound contract for client authorization (ACLs). A grant is keyed by the
 * client TLS fingerprint and the namespaced interface (agent_name +
 * interface_name); any of the three may be the literal "*" wildcard, giving
 * the object scopes exact (agent/iface), agent-wide (agent then *) and
 * global (* then *), and the subject scopes specific fingerprint or any (*).
 * Permission
 * resolution is most-specific-wins: subject dominates (a rule naming the
 * fingerprint beats any "*" rule), then object specificity (exact >
 * agent-wide > global). With no matching rule the baseline is read-open,
 * no-write. The "*" subject is therefore the default for unmatched clients.
 * The platform decides where the grants live (SQLite on the hub).
 */

#define AUTHORIZATION_WILDCARD "*"

typedef struct {
    char fingerprint_hex[IDENTITY_FINGERPRINT_HEX_SIZE];
    char agent_name[REGISTER_AGENT_NAME_SIZE];
    char interface_name[REGISTER_INTERFACE_NAME_SIZE];
    bool can_read;
    bool can_write;
} AclEntry;

typedef struct {
    void *context;
    bool (*read_allowed)(
        void *context,
        const char *fingerprint_hex,
        const char *agent_name,
        const char *interface_name
    );
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
        bool can_read,
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
