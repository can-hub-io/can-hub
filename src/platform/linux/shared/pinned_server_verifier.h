#pragma once

#include <stdbool.h>

#include <openssl/ssl.h>

#include "platform/linux/shared/pin_store.h"

/*
 * Verification of a server certificate during a TLS handshake, shared by
 * every client-side TLS stack (QUIC, TLS-over-TCP). Two modes: TOFU (first
 * contact pins the fingerprint under the given key in the pin store, later
 * contacts must match it) or fixed (the expected fingerprint is injected up
 * front, no store involved). Attach installs the certificate verify
 * callback on the SSL_CTX with this verifier as its argument; the verifier
 * must outlive the context.
 */

#define PINNED_SERVER_VERIFIER_PATH_MAX 512

typedef struct {
    char pin_store_path[PINNED_SERVER_VERIFIER_PATH_MAX];
    char pin_key[PIN_STORE_KEY_MAX];
    char expected_fingerprint[PIN_STORE_FINGERPRINT_HEX_SIZE];
} PinnedServerVerifier;

void PinnedServerVerifier_Attach(
    PinnedServerVerifier *self,
    SSL_CTX *context,
    const char *pin_store_path,
    const char *pin_key
);
void PinnedServerVerifier_AttachFixed(
    PinnedServerVerifier *self,
    SSL_CTX *context,
    const char *expected_fingerprint
);
