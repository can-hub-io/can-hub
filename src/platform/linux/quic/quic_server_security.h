#pragma once

#include <stdbool.h>

#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_ossl.h>
#include <openssl/ssl.h>

/*
 * TLS side of the QUIC server: one OpenSSL context shared by every
 * connection, one session per accepted peer wired to ngtcp2 through the
 * crypto-ossl backend. Client certificates are required (mTLS); the peer
 * fingerprint is the identity the broker pins at REGISTER.
 */
typedef struct {
    SSL_CTX *context;
} QuicServerSecurity;

bool QuicServerSecurity_Init(QuicServerSecurity *self, const char *certificate_file, const char *key_file);
bool QuicServerSecurity_NewSession(
    QuicServerSecurity *self,
    SSL **ssl,
    ngtcp2_crypto_ossl_ctx **tls_context,
    ngtcp2_crypto_conn_ref *connection_ref
);
void QuicServerSecurity_FreeSession(SSL *ssl, ngtcp2_crypto_ossl_ctx *tls_context);
