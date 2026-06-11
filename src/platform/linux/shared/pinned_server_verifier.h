#pragma once

#include <stdbool.h>

#include <openssl/ssl.h>

#include "platform/linux/shared/pin_store.h"

/*
 * TOFU verification of a server certificate during a TLS handshake, shared
 * by every client-side TLS stack (QUIC, TLS-over-TCP): first contact pins
 * the fingerprint under the given key, later contacts must match it.
 * Attach installs the certificate verify callback on the SSL_CTX with this
 * verifier as its argument; the verifier must outlive the context.
 */

#define PINNED_SERVER_VERIFIER_PATH_MAX 512

typedef struct {
    char pin_store_path[PINNED_SERVER_VERIFIER_PATH_MAX];
    char pin_key[PIN_STORE_KEY_MAX];
} PinnedServerVerifier;

void PinnedServerVerifier_Attach(
    PinnedServerVerifier *self,
    SSL_CTX *context,
    const char *pin_store_path,
    const char *pin_key
);
