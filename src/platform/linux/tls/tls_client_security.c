#include "platform/linux/tls/tls_client_security.h"

#include <stdio.h>
#include <string.h>

#define ALPN_PROTOCOL "canhub/0"

#define TLS_PRIORITY \
    "NORMAL:-VERS-ALL:+VERS-TLS1.3:-CIPHER-ALL:+AES-128-GCM:+AES-256-GCM:" \
    "+CHACHA20-POLY1305:%DISABLE_TLS13_COMPAT_MODE"

static bool loadClientIdentity(TlsClientSecurity *self, const TlsClientSecurityConfig *config);

/* ---------- public ---------- */

bool TlsClientSecurity_Init(TlsClientSecurity *self, const TlsClientSecurityConfig *config)
{
    memset(self, 0, sizeof(*self));
    if (gnutls_certificate_allocate_credentials(&self->credentials) != 0) {
        return false;
    }
    if (!loadClientIdentity(self, config)) {
        gnutls_certificate_free_credentials(self->credentials);
        self->credentials = NULL;
        return false;
    }

    if (config != NULL && config->pin_store_path != NULL && config->pin_key != NULL) {
        snprintf(self->pin_store_path, sizeof(self->pin_store_path), "%s", config->pin_store_path);
        snprintf(self->pin_key, sizeof(self->pin_key), "%s", config->pin_key);
        self->pin_enabled = true;
    }

    return true;
}

void TlsClientSecurity_Free(TlsClientSecurity *self)
{
    PinnedServerVerifier_Detach(&self->verifier);
    if (self->credentials != NULL) {
        gnutls_certificate_free_credentials(self->credentials);
        self->credentials = NULL;
    }
}

bool TlsClientSecurity_NewSession(TlsClientSecurity *self, const char *server_host, gnutls_session_t *session)
{
    static const gnutls_datum_t alpn = { (unsigned char *)ALPN_PROTOCOL, sizeof(ALPN_PROTOCOL) - 1 };

    if (gnutls_init(session, GNUTLS_CLIENT | GNUTLS_NONBLOCK) != 0) {
        return false;
    }

    gnutls_priority_set_direct(*session, TLS_PRIORITY, NULL);
    gnutls_credentials_set(*session, GNUTLS_CRD_CERTIFICATE, self->credentials);
    gnutls_alpn_set_protocols(*session, &alpn, 1, GNUTLS_ALPN_MANDATORY);
    gnutls_server_name_set(*session, GNUTLS_NAME_DNS, server_host, strlen(server_host));

    if (self->pin_enabled) {
        PinnedServerVerifier_Attach(&self->verifier, *session, self->credentials, self->pin_store_path, self->pin_key);
    }

    return true;
}

/* ---------- private ---------- */

static bool loadClientIdentity(TlsClientSecurity *self, const TlsClientSecurityConfig *config)
{
    int32_t result;

    if (config == NULL || config->certificate_path == NULL || config->key_path == NULL) {
        return true;
    }

    result = gnutls_certificate_set_x509_key_file(
        self->credentials,
        config->certificate_path,
        config->key_path,
        GNUTLS_X509_FMT_PEM
    );

    return result == 0;
}
