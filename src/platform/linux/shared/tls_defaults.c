#include "platform/linux/shared/tls_defaults.h"

#define TLS13_CIPHERSUITES "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256"

static const uint8_t alpn_wire[] = { 8, 'c', 'a', 'n', 'h', 'u', 'b', '/', '0' };

static int acceptAnyClientCertificate(X509_STORE_CTX *store_context, void *argument);
static int selectAlpnProtocol(
    SSL *ssl,
    const unsigned char **out,
    unsigned char *out_length,
    const unsigned char *in,
    unsigned int in_length,
    void *argument
);

/* ---------- public ---------- */

SSL_CTX *TlsDefaults_NewContext(const SSL_METHOD *method)
{
    SSL_CTX *context = SSL_CTX_new(method);

    if (context == NULL) {
        return NULL;
    }

    if (SSL_CTX_set_min_proto_version(context, TLS1_3_VERSION) != 1
        || SSL_CTX_set_max_proto_version(context, TLS1_3_VERSION) != 1
        || SSL_CTX_set_ciphersuites(context, TLS13_CIPHERSUITES) != 1) {
        SSL_CTX_free(context);
        return NULL;
    }

    return context;
}

bool TlsDefaults_LoadIdentity(SSL_CTX *context, const char *certificate_path, const char *key_path)
{
    return SSL_CTX_use_certificate_chain_file(context, certificate_path) == 1
        && SSL_CTX_use_PrivateKey_file(context, key_path, SSL_FILETYPE_PEM) == 1;
}

void TlsDefaults_ConfigureServerContext(SSL_CTX *context)
{
    SSL_CTX_set_verify(context, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
    SSL_CTX_set_cert_verify_callback(context, acceptAnyClientCertificate, NULL);
    SSL_CTX_set_alpn_select_cb(context, selectAlpnProtocol, NULL);
}

void TlsDefaults_ConfigureClientSession(SSL *ssl, const char *server_host)
{
    SSL_set_alpn_protos(ssl, alpn_wire, sizeof(alpn_wire));
    SSL_set_tlsext_host_name(ssl, server_host);
    SSL_set_mode(ssl, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    SSL_set_connect_state(ssl);
}

void TlsDefaults_ConfigureServerSession(SSL *ssl)
{
    SSL_set_mode(ssl, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    SSL_set_accept_state(ssl);
}

/* ---------- private ---------- */

static int acceptAnyClientCertificate(X509_STORE_CTX *store_context, void *argument)
{
    (void)store_context;
    (void)argument;

    return 1;
}

static int selectAlpnProtocol(
    SSL *ssl,
    const unsigned char **out,
    unsigned char *out_length,
    const unsigned char *in,
    unsigned int in_length,
    void *argument
)
{
    (void)ssl;
    (void)argument;

    if (SSL_select_next_proto((unsigned char **)out, out_length, alpn_wire, sizeof(alpn_wire), in, in_length)
        != OPENSSL_NPN_NEGOTIATED) {
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    }

    return SSL_TLSEXT_ERR_OK;
}
