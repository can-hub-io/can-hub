#include "platform/linux/quic/quic_server_security.h"

#include "platform/linux/shared/tls_defaults.h"

static bool cryptoBackendReady(void);

/* ---------- public ---------- */

bool QuicServerSecurity_Init(QuicServerSecurity *self, const char *certificate_file, const char *key_file)
{
    if (!cryptoBackendReady()) {
        return false;
    }

    self->context = TlsDefaults_NewContext(TLS_server_method());
    if (self->context == NULL) {
        return false;
    }

    if (!TlsDefaults_LoadIdentity(self->context, certificate_file, key_file)) {
        SSL_CTX_free(self->context);
        self->context = NULL;
        return false;
    }
    TlsDefaults_ConfigureServerContext(self->context);
    if (!QuicTlsBackend_ConfigureServerContext(self->context)) {
        SSL_CTX_free(self->context);
        self->context = NULL;
        return false;
    }

    return true;
}

bool QuicServerSecurity_NewSession(
    QuicServerSecurity *self,
    SSL **ssl,
    QuicTlsContext **tls_context,
    ngtcp2_crypto_conn_ref *connection_ref
)
{
    *tls_context = NULL;
    *ssl = SSL_new(self->context);
    if (*ssl == NULL) {
        return false;
    }

    if (!QuicTlsBackend_NewSession(tls_context, *ssl, true)) {
        QuicServerSecurity_FreeSession(*ssl, *tls_context);
        *ssl = NULL;
        *tls_context = NULL;
        return false;
    }

    SSL_set_app_data(*ssl, connection_ref);
    TlsDefaults_ConfigureServerSession(*ssl);

    return true;
}

void QuicServerSecurity_FreeSession(SSL *ssl, QuicTlsContext *tls_context)
{
    if (tls_context != NULL) {
        QuicTlsBackend_FreeSession(tls_context);
    }
    if (ssl != NULL) {
        SSL_free(ssl);
    }
}

/* ---------- private ---------- */

static bool cryptoBackendReady(void)
{
    return QuicTlsBackend_Ready();
}
