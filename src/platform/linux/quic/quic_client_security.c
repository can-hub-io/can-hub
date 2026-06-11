#include "platform/linux/quic/quic_client_security.h"

#include <string.h>

#include "platform/linux/shared/tls_defaults.h"

static bool loadClientIdentity(QuicClientSecurity *self, const QuicClientSecurityConfig *config);
static bool cryptoBackendReady(void);

/* ---------- public ---------- */

bool QuicClientSecurity_Init(
    QuicClientSecurity *self,
    const char *server_host,
    ngtcp2_crypto_conn_ref *connection_ref,
    const QuicClientSecurityConfig *config
)
{
    memset(self, 0, sizeof(*self));

    if (!cryptoBackendReady()) {
        return false;
    }
    self->context = TlsDefaults_NewContext(TLS_client_method());
    if (self->context == NULL) {
        return false;
    }
    if (!loadClientIdentity(self, config)) {
        QuicClientSecurity_Free(self);
        return false;
    }
    if (config != NULL && config->pinned_fingerprint != NULL) {
        PinnedServerVerifier_AttachFixed(&self->verifier, self->context, config->pinned_fingerprint);
    } else if (config != NULL && config->pin_store_path != NULL && config->pin_key != NULL) {
        PinnedServerVerifier_Attach(&self->verifier, self->context, config->pin_store_path, config->pin_key);
    }

    self->ssl = SSL_new(self->context);
    if (self->ssl == NULL) {
        QuicClientSecurity_Free(self);
        return false;
    }
    if (ngtcp2_crypto_ossl_ctx_new(&self->tls_context, self->ssl) != 0) {
        QuicClientSecurity_Free(self);
        return false;
    }
    if (ngtcp2_crypto_ossl_configure_client_session(self->ssl) != 0) {
        QuicClientSecurity_Free(self);
        return false;
    }

    SSL_set_app_data(self->ssl, connection_ref);
    TlsDefaults_ConfigureClientSession(self->ssl, server_host);

    return true;
}

void QuicClientSecurity_Free(QuicClientSecurity *self)
{
    if (self->tls_context != NULL) {
        ngtcp2_crypto_ossl_ctx_del(self->tls_context);
        self->tls_context = NULL;
    }
    if (self->ssl != NULL) {
        SSL_free(self->ssl);
        self->ssl = NULL;
    }
    if (self->context != NULL) {
        SSL_CTX_free(self->context);
        self->context = NULL;
    }
}

/* ---------- private ---------- */

static bool loadClientIdentity(QuicClientSecurity *self, const QuicClientSecurityConfig *config)
{
    if (config == NULL || config->certificate_path == NULL || config->key_path == NULL) {
        return true;
    }

    return TlsDefaults_LoadIdentity(self->context, config->certificate_path, config->key_path);
}

static bool cryptoBackendReady(void)
{
    static bool initialized;

    if (!initialized) {
        initialized = ngtcp2_crypto_ossl_init() == 0;
    }

    return initialized;
}
