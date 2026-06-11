#include "platform/linux/tls/tls_server_security.h"

#include "platform/linux/shared/tls_defaults.h"

/* ---------- public ---------- */

bool TlsServerSecurity_Init(TlsServerSecurity *self, const char *certificate_file, const char *key_file)
{
    self->context = TlsDefaults_NewContext(TLS_server_method());
    if (self->context == NULL) {
        return false;
    }

    if (!TlsDefaults_LoadIdentity(self->context, certificate_file, key_file)) {
        TlsServerSecurity_Free(self);
        return false;
    }
    TlsDefaults_ConfigureServerContext(self->context);

    return true;
}

void TlsServerSecurity_Free(TlsServerSecurity *self)
{
    if (self->context != NULL) {
        SSL_CTX_free(self->context);
        self->context = NULL;
    }
}

bool TlsServerSecurity_NewSession(TlsServerSecurity *self, SSL **ssl)
{
    *ssl = SSL_new(self->context);
    if (*ssl == NULL) {
        return false;
    }

    TlsDefaults_ConfigureServerSession(*ssl);

    return true;
}
