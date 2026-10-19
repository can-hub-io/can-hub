#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <picotls.h>

#include "platform/linux/shared/pin_store.h"
#include "platform/linux/shared/tls_peer_certificate.h"

/*
 * Verification of a server certificate during a TLS handshake, shared by
 * every client-side TLS stack (QUIC, TLS-over-TCP). Two modes: TOFU (first
 * contact pins the fingerprint under the given key in the pin store, later
 * contacts must match it) or fixed (the expected fingerprint is injected up
 * front, no store involved). Attach installs the verifier on the picotls
 * context; the verifier must outlive it.
 *
 * A key the store does not know yet is accepted, and recorded under the
 * existing entry, when the hub presents a fingerprint already pinned: one hub
 * reached under a new address opens no new first contact. A new fingerprint
 * under a new key is a new hub: the any-hub policy pins it, the one-hub
 * policy refuses it once the store trusts some hub.
 */

#define PINNED_SERVER_VERIFIER_PATH_MAX 512

typedef enum tpin_policy_e {
    kPIN_POLICY_ANY_HUB = 0,
    kPIN_POLICY_ONE_HUB,
    kPIN_POLICY_MAX,
} TPIN_POLICY;

typedef struct {
    ptls_verify_certificate_t super;
    TlsPeerResolver resolve_peer;
    char pin_store_path[PINNED_SERVER_VERIFIER_PATH_MAX];
    char pin_key[PIN_STORE_KEY_MAX];
    uint8_t policy;
    char expected_fingerprint[PIN_STORE_FINGERPRINT_HEX_SIZE];
    char pending_fingerprint[PIN_STORE_FINGERPRINT_HEX_SIZE];
} PinnedServerVerifier;

void PinnedServerVerifier_Attach(
    PinnedServerVerifier *self,
    ptls_context_t *context,
    TlsPeerResolver resolve_peer,
    const char *pin_store_path,
    const char *pin_key,
    uint8_t policy
);
void PinnedServerVerifier_AttachFixed(
    PinnedServerVerifier *self,
    ptls_context_t *context,
    TlsPeerResolver resolve_peer,
    const char *expected_fingerprint
);
void PinnedServerVerifier_CommitPin(PinnedServerVerifier *self);
