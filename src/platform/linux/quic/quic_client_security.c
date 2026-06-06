#include "platform/linux/quic/quic_client_security.h"

#include <stdio.h>
#include <string.h>

#include <ngtcp2/ngtcp2_crypto_gnutls.h>

#include "platform/linux/shared/tls_identity.h"

#define ALPN_PROTOCOL "canhub/0"
#define SESSIONS_MAX 4

#define TLS_PRIORITY \
    "NORMAL:-VERS-ALL:+VERS-TLS1.3:-CIPHER-ALL:+AES-128-GCM:+AES-256-GCM:" \
    "+CHACHA20-POLY1305:%DISABLE_TLS13_COMPAT_MODE"

static QuicClientSecurity *registered_sessions[SESSIONS_MAX];

static bool loadClientIdentity(QuicClientSecurity *self, const QuicClientSecurityConfig *config);
static void registerSession(QuicClientSecurity *self);
static void unregisterSession(QuicClientSecurity *self);
static QuicClientSecurity *findBySession(gnutls_session_t session);
static int verifyPinnedServer(gnutls_session_t session);

/* ---------- public ---------- */

bool QuicClientSecurity_Init(
    QuicClientSecurity *self,
    const char *server_host,
    ngtcp2_crypto_conn_ref *connection_ref,
    const QuicClientSecurityConfig *config
)
{
    static const gnutls_datum_t alpn = { (unsigned char *)ALPN_PROTOCOL, sizeof(ALPN_PROTOCOL) - 1 };

    memset(self, 0, sizeof(*self));
    if (gnutls_certificate_allocate_credentials(&self->credentials) != 0) {
        return false;
    }
    if (!loadClientIdentity(self, config)) {
        gnutls_certificate_free_credentials(self->credentials);
        self->credentials = NULL;
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

    if (config != NULL && config->pin_store_path != NULL && config->pin_key != NULL) {
        snprintf(self->pin_store_path, sizeof(self->pin_store_path), "%s", config->pin_store_path);
        snprintf(self->pin_key, sizeof(self->pin_key), "%s", config->pin_key);
        gnutls_certificate_set_verify_function(self->credentials, verifyPinnedServer);
        registerSession(self);
    }

    if (ngtcp2_crypto_gnutls_configure_client_session(self->session) != 0) {
        QuicClientSecurity_Free(self);
        return false;
    }
    gnutls_session_set_ptr(self->session, connection_ref);

    return true;
}

void QuicClientSecurity_Free(QuicClientSecurity *self)
{
    unregisterSession(self);
    if (self->session != NULL) {
        gnutls_deinit(self->session);
        self->session = NULL;
    }
    if (self->credentials != NULL) {
        gnutls_certificate_free_credentials(self->credentials);
        self->credentials = NULL;
    }
}

/* ---------- private ---------- */

static bool loadClientIdentity(QuicClientSecurity *self, const QuicClientSecurityConfig *config)
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

static void registerSession(QuicClientSecurity *self)
{
    uint8_t i;

    for(i=0; i<SESSIONS_MAX; i++) {
        if (registered_sessions[i] == NULL) {
            registered_sessions[i] = self;
            return;
        }
    }
}

static void unregisterSession(QuicClientSecurity *self)
{
    uint8_t i;

    for(i=0; i<SESSIONS_MAX; i++) {
        if (registered_sessions[i] == self) {
            registered_sessions[i] = NULL;
        }
    }
}

static QuicClientSecurity *findBySession(gnutls_session_t session)
{
    uint8_t i;

    for(i=0; i<SESSIONS_MAX; i++) {
        if (registered_sessions[i] != NULL && registered_sessions[i]->session == session) {
            return registered_sessions[i];
        }
    }

    return NULL;
}

static int verifyPinnedServer(gnutls_session_t session)
{
    QuicClientSecurity *self = findBySession(session);
    const gnutls_datum_t *certificates;
    unsigned int certificate_count;
    char fingerprint[TLS_IDENTITY_FINGERPRINT_HEX_SIZE];
    char pinned[PIN_STORE_FINGERPRINT_HEX_SIZE];

    if (self == NULL) {
        return GNUTLS_E_CERTIFICATE_ERROR;
    }

    certificates = gnutls_certificate_get_peers(session, &certificate_count);
    if (certificates == NULL || certificate_count == 0) {
        return GNUTLS_E_CERTIFICATE_ERROR;
    }
    if (!TlsIdentity_FingerprintOfDer(&certificates[0], fingerprint)) {
        return GNUTLS_E_CERTIFICATE_ERROR;
    }

    if (!PinStore_Lookup(self->pin_store_path, self->pin_key, pinned)) {
        fprintf(stderr, "pinning hub %s fingerprint %s\n", self->pin_key, fingerprint);
        return PinStore_Append(self->pin_store_path, self->pin_key, fingerprint) ? 0 : GNUTLS_E_CERTIFICATE_ERROR;
    }
    if (strcmp(pinned, fingerprint) != 0) {
        fprintf(stderr, "hub %s fingerprint changed, rejecting connection\n", self->pin_key);
        return GNUTLS_E_CERTIFICATE_ERROR;
    }

    return 0;
}
