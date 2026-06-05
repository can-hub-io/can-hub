#include "platform/linux/quic/quic_client_security.h"

#include <string.h>

#include <ngtcp2/ngtcp2_crypto_gnutls.h>

#define ALPN_PROTOCOL "canhub/0"

#define TLS_PRIORITY \
    "NORMAL:-VERS-ALL:+VERS-TLS1.3:-CIPHER-ALL:+AES-128-GCM:+AES-256-GCM:" \
    "+CHACHA20-POLY1305:%DISABLE_TLS13_COMPAT_MODE"

/* ---------- public ---------- */

bool QuicClientSecurity_Init(
    QuicClientSecurity *self,
    const char *server_host,
    ngtcp2_crypto_conn_ref *connection_ref
)
{
    static const gnutls_datum_t alpn = { (unsigned char *)ALPN_PROTOCOL, sizeof(ALPN_PROTOCOL) - 1 };

    if (gnutls_certificate_allocate_credentials(&self->credentials) != 0) {
        return false;
    }
    if (gnutls_init(&self->session, GNUTLS_CLIENT) != 0) {
        gnutls_certificate_free_credentials(self->credentials);
        self->credentials = NULL;
        return false;
    }

    gnutls_priority_set_direct(self->session, TLS_PRIORITY, NULL);
    gnutls_credentials_set(self->session, GNUTLS_CRD_CERTIFICATE, self->credentials);
    gnutls_alpn_set_protocols(self->session, &alpn, 1, GNUTLS_ALPN_MANDATORY);
    gnutls_server_name_set(self->session, GNUTLS_NAME_DNS, server_host, strlen(server_host));

    if (ngtcp2_crypto_gnutls_configure_client_session(self->session) != 0) {
        QuicClientSecurity_Free(self);
        return false;
    }
    gnutls_session_set_ptr(self->session, connection_ref);

    return true;
}

void QuicClientSecurity_Free(QuicClientSecurity *self)
{
    if (self->session != NULL) {
        gnutls_deinit(self->session);
        self->session = NULL;
    }
    if (self->credentials != NULL) {
        gnutls_certificate_free_credentials(self->credentials);
        self->credentials = NULL;
    }
}
