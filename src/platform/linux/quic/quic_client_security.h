#pragma once

#include <stdbool.h>

#include <gnutls/gnutls.h>
#include <ngtcp2/ngtcp2_crypto.h>

#include "platform/linux/shared/pinned_server_verifier.h"

/*
 * TLS side of a QUIC client connection: GnuTLS session + credentials wired
 * to ngtcp2. Presents the agent identity certificate (mTLS) and verifies
 * the server against the TOFU pin store through PinnedServerVerifier.
 */

typedef struct {
    const char *certificate_path;
    const char *key_path;
    const char *pin_store_path;
    const char *pin_key;
} QuicClientSecurityConfig;

typedef struct {
    gnutls_session_t session;
    gnutls_certificate_credentials_t credentials;
    PinnedServerVerifier verifier;
} QuicClientSecurity;

bool QuicClientSecurity_Init(
    QuicClientSecurity *self,
    const char *server_host,
    ngtcp2_crypto_conn_ref *connection_ref,
    const QuicClientSecurityConfig *config
);
void QuicClientSecurity_Free(QuicClientSecurity *self);
