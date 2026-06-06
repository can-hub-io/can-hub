#include "platform/linux/quic/quic_server_security.h"

#include <ngtcp2/ngtcp2_crypto_gnutls.h>

#define ALPN_PROTOCOL "canhub/0"

#define TLS_PRIORITY \
    "NORMAL:-VERS-ALL:+VERS-TLS1.3:-CIPHER-ALL:+AES-128-GCM:+AES-256-GCM:" \
    "+CHACHA20-POLY1305:%DISABLE_TLS13_COMPAT_MODE"

/* ---------- public ---------- */

bool QuicServerSecurity_Init(QuicServerSecurity *self, const char *certificate_file, const char *key_file)
{
    int result;

    if (gnutls_certificate_allocate_credentials(&self->credentials) != 0) {
        return false;
    }

    result = gnutls_certificate_set_x509_key_file(
        self->credentials,
        certificate_file,
        key_file,
        GNUTLS_X509_FMT_PEM
    );
    if (result != 0) {
        gnutls_certificate_free_credentials(self->credentials);
        self->credentials = NULL;
        return false;
    }

    return true;
}

bool QuicServerSecurity_NewSession(
    QuicServerSecurity *self,
    gnutls_session_t *session,
    ngtcp2_crypto_conn_ref *connection_ref
)
{
    static const gnutls_datum_t alpn = { (unsigned char *)ALPN_PROTOCOL, sizeof(ALPN_PROTOCOL) - 1 };

    if (gnutls_init(session, GNUTLS_SERVER) != 0) {
        return false;
    }

    gnutls_priority_set_direct(*session, TLS_PRIORITY, NULL);
    gnutls_credentials_set(*session, GNUTLS_CRD_CERTIFICATE, self->credentials);
    gnutls_alpn_set_protocols(*session, &alpn, 1, GNUTLS_ALPN_MANDATORY);
    gnutls_certificate_server_set_request(*session, GNUTLS_CERT_REQUIRE);

    if (ngtcp2_crypto_gnutls_configure_server_session(*session) != 0) {
        gnutls_deinit(*session);
        *session = NULL;
        return false;
    }

    gnutls_session_set_ptr(*session, connection_ref);

    return true;
}

void QuicServerSecurity_FreeSession(gnutls_session_t session)
{
    if (session != NULL) {
        gnutls_deinit(session);
    }
}
