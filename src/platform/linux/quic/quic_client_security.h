#pragma once

#include <stdbool.h>

#include <gnutls/gnutls.h>
#include <ngtcp2/ngtcp2_crypto.h>

#include "platform/linux/shared/pin_store.h"

/*
 * TLS side of a QUIC client connection: GnuTLS session + credentials wired
 * to ngtcp2. Presents the agent identity certificate (mTLS) and verifies
 * the server against the TOFU pin store: first contact records the
 * fingerprint, later contacts must match it.
 */

#define QUIC_CLIENT_SECURITY_PATH_MAX 512

typedef struct {
    const char *certificate_path;
    const char *key_path;
    const char *pin_store_path;
    const char *pin_key;
} QuicClientSecurityConfig;

typedef struct {
    gnutls_session_t session;
    gnutls_certificate_credentials_t credentials;
    char pin_store_path[QUIC_CLIENT_SECURITY_PATH_MAX];
    char pin_key[PIN_STORE_KEY_MAX];
} QuicClientSecurity;

bool QuicClientSecurity_Init(
    QuicClientSecurity *self,
    const char *server_host,
    ngtcp2_crypto_conn_ref *connection_ref,
    const QuicClientSecurityConfig *config
);
void QuicClientSecurity_Free(QuicClientSecurity *self);
