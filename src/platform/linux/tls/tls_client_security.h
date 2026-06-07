#pragma once

#include <stdbool.h>

#include <gnutls/gnutls.h>

#include "platform/linux/shared/pinned_server_verifier.h"

/*
 * TLS side of a client connection over TCP: GnuTLS session + credentials.
 * Presents the local identity certificate (mTLS, required by the hub) and
 * verifies the hub against the TOFU pin store through PinnedServerVerifier.
 */

typedef struct {
    const char *certificate_path;
    const char *key_path;
    const char *pin_store_path;
    const char *pin_key;
} TlsClientSecurityConfig;

typedef struct {
    gnutls_certificate_credentials_t credentials;
    PinnedServerVerifier verifier;
    char pin_store_path[PINNED_SERVER_VERIFIER_PATH_MAX];
    char pin_key[PIN_STORE_KEY_MAX];
    bool pin_enabled;
} TlsClientSecurity;

bool TlsClientSecurity_Init(TlsClientSecurity *self, const TlsClientSecurityConfig *config);
void TlsClientSecurity_Free(TlsClientSecurity *self);
bool TlsClientSecurity_NewSession(TlsClientSecurity *self, const char *server_host, gnutls_session_t *session);
