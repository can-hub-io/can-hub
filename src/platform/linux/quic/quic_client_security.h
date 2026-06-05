#pragma once

#include <stdbool.h>

#include <gnutls/gnutls.h>
#include <ngtcp2/ngtcp2_crypto.h>

/*
 * TLS side of a QUIC client connection: GnuTLS session + credentials wired
 * to ngtcp2. Certificate verification (TOFU pinning) lands here later.
 */
typedef struct {
    gnutls_session_t session;
    gnutls_certificate_credentials_t credentials;
} QuicClientSecurity;

bool QuicClientSecurity_Init(
    QuicClientSecurity *self,
    const char *server_host,
    ngtcp2_crypto_conn_ref *connection_ref
);
void QuicClientSecurity_Free(QuicClientSecurity *self);
