#include "platform/linux/tls/tls_client_security.h"

#include "platform/linux/shared/tls_defaults.h"

static bool loadClientIdentity(TlsClientSecurity *self, const TlsClientSecurityConfig *config);

/* ---------- public ---------- */

bool TlsClientSecurity_Init(TlsClientSecurity *self, const TlsClientSecurityConfig *config)
{
    self->context = TlsDefaults_NewContext(TLS_client_method());
    if (self->context == NULL) {
        return false;
    }
    if (!loadClientIdentity(self, config)) {
        TlsClientSecurity_Free(self);
        return false;
    }

    if (config != NULL && config->pinned_fingerprint != NULL) {
        PinnedServerVerifier_AttachFixed(&self->verifier, self->context, config->pinned_fingerprint);
    } else if (config != NULL && config->pin_store_path != NULL && config->pin_key != NULL) {
        PinnedServerVerifier_Attach(&self->verifier, self->context, config->pin_store_path, config->pin_key);
    }

    return true;
}

void TlsClientSecurity_Free(TlsClientSecurity *self)
{
    if (self->context != NULL) {
        SSL_CTX_free(self->context);
        self->context = NULL;
    }
}

bool TlsClientSecurity_NewSession(TlsClientSecurity *self, const char *server_host, SSL **ssl)
{
    *ssl = SSL_new(self->context);
    if (*ssl == NULL) {
        return false;
    }

    TlsDefaults_ConfigureClientSession(*ssl, server_host);

    return true;
}

/* ---------- private ---------- */

static bool loadClientIdentity(TlsClientSecurity *self, const TlsClientSecurityConfig *config)
{
    if (config == NULL || config->certificate_path == NULL || config->key_path == NULL) {
        return true;
    }

    return TlsDefaults_LoadIdentity(self->context, config->certificate_path, config->key_path);
}
