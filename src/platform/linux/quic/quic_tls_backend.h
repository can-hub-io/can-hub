#pragma once

/*
 * One shape for the two ngtcp2 crypto backends.
 *
 * The ossl backend wraps every session in an ngtcp2_crypto_ossl_ctx, needs a
 * one-off global init, and configures each session individually. The wolfssl
 * backend has none of that: it configures the SSL_CTX once and the native
 * handle is the SSL itself. This hides the difference so the QUIC modules keep
 * one code path.
 */

#include <stdbool.h>

#include <openssl/ssl.h>

#if defined(CAN_HUB_TLS_BORINGSSL)

/* AWS-LC and BoringSSL share ngtcp2's boringssl backend, which has the same
   shape as the wolfssl one: configure the context, and the native handle is
   the SSL itself. */

typedef SSL QuicTlsContext;

#include <ngtcp2/ngtcp2_crypto_boringssl.h>

static inline bool QuicTlsBackend_Ready(void)
{
    return true;
}

static inline bool QuicTlsBackend_ConfigureClientContext(SSL_CTX *context)
{
    return ngtcp2_crypto_boringssl_configure_client_context(context) == 0;
}

static inline bool QuicTlsBackend_ConfigureServerContext(SSL_CTX *context)
{
    return ngtcp2_crypto_boringssl_configure_server_context(context) == 0;
}

static inline bool QuicTlsBackend_NewSession(QuicTlsContext **tls_context, SSL *ssl, bool server)
{
    (void)server;
    *tls_context = ssl;

    return true;
}

static inline void QuicTlsBackend_FreeSession(QuicTlsContext *tls_context)
{
    (void)tls_context;
}

#elif defined(CAN_HUB_TLS_WOLFSSL)

typedef SSL QuicTlsContext;

#include <ngtcp2/ngtcp2_crypto_wolfssl.h>

static inline bool QuicTlsBackend_Ready(void)
{
    return true;
}

static inline bool QuicTlsBackend_ConfigureClientContext(SSL_CTX *context)
{
    return ngtcp2_crypto_wolfssl_configure_client_context(context) == 0;
}

static inline bool QuicTlsBackend_ConfigureServerContext(SSL_CTX *context)
{
    return ngtcp2_crypto_wolfssl_configure_server_context(context) == 0;
}

static inline bool QuicTlsBackend_NewSession(QuicTlsContext **tls_context, SSL *ssl, bool server)
{
    (void)server;
    *tls_context = ssl;

    return true;
}

static inline void QuicTlsBackend_FreeSession(QuicTlsContext *tls_context)
{
    (void)tls_context;
}

#else

#include <ngtcp2/ngtcp2_crypto_ossl.h>

typedef ngtcp2_crypto_ossl_ctx QuicTlsContext;

static inline bool QuicTlsBackend_Ready(void)
{
    static bool initialized;

    if (!initialized) {
        initialized = ngtcp2_crypto_ossl_init() == 0;
    }

    return initialized;
}

static inline bool QuicTlsBackend_ConfigureClientContext(SSL_CTX *context)
{
    (void)context;

    return true;
}

static inline bool QuicTlsBackend_ConfigureServerContext(SSL_CTX *context)
{
    (void)context;

    return true;
}

static inline bool QuicTlsBackend_NewSession(QuicTlsContext **tls_context, SSL *ssl, bool server)
{
    if (ngtcp2_crypto_ossl_ctx_new(tls_context, ssl) != 0) {
        return false;
    }
    if (server) {
        return ngtcp2_crypto_ossl_configure_server_session(ssl) == 0;
    }

    return ngtcp2_crypto_ossl_configure_client_session(ssl) == 0;
}

static inline void QuicTlsBackend_FreeSession(QuicTlsContext *tls_context)
{
    if (tls_context != NULL) {
        ngtcp2_crypto_ossl_ctx_del(tls_context);
    }
}

#endif
