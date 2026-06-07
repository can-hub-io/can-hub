#pragma once

#include <stdbool.h>

#include <gnutls/gnutls.h>

#include "platform/linux/shared/pin_store.h"

/*
 * TOFU verification of a server certificate during a GnuTLS handshake,
 * shared by every client-side TLS stack (QUIC, TLS-over-TCP): first contact
 * pins the fingerprint under the given key, later contacts must match it.
 * Attach installs the verify callback on the credentials and registers the
 * session so the callback can find its pin configuration.
 */

#define PINNED_SERVER_VERIFIER_PATH_MAX 512

typedef struct {
    gnutls_session_t session;
    char pin_store_path[PINNED_SERVER_VERIFIER_PATH_MAX];
    char pin_key[PIN_STORE_KEY_MAX];
} PinnedServerVerifier;

void PinnedServerVerifier_Attach(
    PinnedServerVerifier *self,
    gnutls_session_t session,
    gnutls_certificate_credentials_t credentials,
    const char *pin_store_path,
    const char *pin_key
);
void PinnedServerVerifier_Detach(PinnedServerVerifier *self);
