#pragma once

#include <stdbool.h>

#include <openssl/ssl.h>

/*
 * TLS context of the hub listener over TCP: the hub identity keypair plus
 * per-peer server sessions that require a client certificate (mTLS) — the
 * peer fingerprint is the identity the broker pins at REGISTER.
 */
typedef struct {
    SSL_CTX *context;
} TlsServerSecurity;

bool TlsServerSecurity_Init(TlsServerSecurity *self, const char *certificate_file, const char *key_file);
void TlsServerSecurity_Free(TlsServerSecurity *self);
bool TlsServerSecurity_NewSession(TlsServerSecurity *self, SSL **ssl);
