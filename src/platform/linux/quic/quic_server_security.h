#pragma once

#include <stdbool.h>

#include <gnutls/gnutls.h>
#include <ngtcp2/ngtcp2_crypto.h>

/*
 * TLS side of the QUIC server: certificate credentials shared by every
 * connection, one GnuTLS session per accepted peer. Certificate and key
 * come from files; auto-generation and TOFU pinning land in the hardening
 * epic.
 */
typedef struct {
    gnutls_certificate_credentials_t credentials;
} QuicServerSecurity;

bool QuicServerSecurity_Init(QuicServerSecurity *self, const char *certificate_file, const char *key_file);
bool QuicServerSecurity_NewSession(
    QuicServerSecurity *self,
    gnutls_session_t *session,
    ngtcp2_crypto_conn_ref *connection_ref
);
void QuicServerSecurity_FreeSession(gnutls_session_t session);
