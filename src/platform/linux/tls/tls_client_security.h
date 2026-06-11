#pragma once

#include <stdbool.h>

#include <openssl/ssl.h>

#include "platform/linux/shared/pinned_server_verifier.h"

/*
 * TLS side of a client connection over TCP: OpenSSL context + sessions.
 * Presents the local identity certificate (mTLS, required by the hub) and
 * verifies the hub against the TOFU pin store through PinnedServerVerifier.
 */

typedef struct {
    const char *certificate_path;
    const char *key_path;
    const char *pin_store_path;
    const char *pin_key;
    const char *pinned_fingerprint;
} TlsClientSecurityConfig;

typedef struct {
    SSL_CTX *context;
    PinnedServerVerifier verifier;
} TlsClientSecurity;

bool TlsClientSecurity_Init(TlsClientSecurity *self, const TlsClientSecurityConfig *config);
void TlsClientSecurity_Free(TlsClientSecurity *self);
bool TlsClientSecurity_NewSession(TlsClientSecurity *self, const char *server_host, SSL **ssl);
