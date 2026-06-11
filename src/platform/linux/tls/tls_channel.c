#include "platform/linux/tls/tls_channel.h"

#include <string.h>

#include <openssl/x509.h>

#include "platform/linux/shared/tls_identity.h"

#define READ_CHUNK_SIZE 2048

static bool handleSslError(TlsChannel *self, int32_t error);

/* ---------- public ---------- */

void TlsChannel_Reset(TlsChannel *self)
{
    memset(self, 0, sizeof(*self));
    self->fd = TLS_CHANNEL_NO_SOCKET;
    MessageFramer_Reset(&self->framer);
}

void TlsChannel_Bind(TlsChannel *self, int32_t fd, SSL *ssl)
{
    TlsChannel_Reset(self);
    self->fd = fd;
    self->ssl = ssl;
    self->state = kTLS_CHANNEL_STATE_HANDSHAKING;
    SSL_set_fd(ssl, fd);
}

void TlsChannel_Close(TlsChannel *self)
{
    if (self->ssl != NULL) {
        SSL_free(self->ssl);
    }

    TlsChannel_Reset(self);
}

bool TlsChannel_IsBound(const TlsChannel *self)
{
    return self->fd != TLS_CHANNEL_NO_SOCKET;
}

bool TlsChannel_IsEstablished(const TlsChannel *self)
{
    return self->state == kTLS_CHANNEL_STATE_ESTABLISHED;
}

bool TlsChannel_Pump(TlsChannel *self)
{
    int32_t result;

    if (self->state != kTLS_CHANNEL_STATE_HANDSHAKING) {
        return true;
    }

    result = SSL_do_handshake(self->ssl);
    if (result == 1) {
        self->state = kTLS_CHANNEL_STATE_ESTABLISHED;
        self->want_write = false;
        return true;
    }

    return handleSslError(self, SSL_get_error(self->ssl, result));
}

bool TlsChannel_Receive(TlsChannel *self)
{
    uint8_t chunk[READ_CHUNK_SIZE];
    size_t bytes_received;
    int32_t result;

    if (self->state != kTLS_CHANNEL_STATE_ESTABLISHED) {
        return true;
    }

    for (;;) {
        result = SSL_read_ex(self->ssl, chunk, sizeof(chunk), &bytes_received);
        if (result != 1) {
            return handleSslError(self, SSL_get_error(self->ssl, result));
        }

        if (!MessageFramer_Push(&self->framer, chunk, bytes_received)) {
            return false;
        }
    }
}

size_t TlsChannel_FreeTxSpace(const TlsChannel *self)
{
    return TLS_CHANNEL_TX_BACKLOG_SIZE - self->tx_used;
}

bool TlsChannel_Queue(TlsChannel *self, const uint8_t *data, size_t size)
{
    if (self->tx_used + size > TLS_CHANNEL_TX_BACKLOG_SIZE) {
        return false;
    }

    memcpy(self->tx_backlog + self->tx_used, data, size);
    self->tx_used += size;

    return true;
}

bool TlsChannel_Flush(TlsChannel *self)
{
    size_t bytes_sent;
    int32_t result;

    if (self->state != kTLS_CHANNEL_STATE_ESTABLISHED) {
        return true;
    }

    while (self->tx_used > 0) {
        result = SSL_write_ex(self->ssl, self->tx_backlog, self->tx_used, &bytes_sent);
        if (result != 1) {
            return handleSslError(self, SSL_get_error(self->ssl, result));
        }

        memmove(self->tx_backlog, self->tx_backlog + bytes_sent, self->tx_used - bytes_sent);
        self->tx_used -= bytes_sent;
    }
    self->want_write = false;

    return true;
}

bool TlsChannel_WantsWrite(const TlsChannel *self)
{
    if (self->state == kTLS_CHANNEL_STATE_HANDSHAKING) {
        return self->want_write;
    }

    return self->tx_used > 0 || self->want_write;
}

bool TlsChannel_PeerFingerprint(const TlsChannel *self, char *fingerprint_hex)
{
    X509 *certificate = SSL_get0_peer_certificate(self->ssl);
    uint8_t *der = NULL;
    int der_size;
    bool computed = false;

    if (certificate == NULL) {
        return false;
    }

    der_size = i2d_X509(certificate, &der);
    if (der_size > 0) {
        computed = TlsIdentity_FingerprintOfDer(der, (size_t)der_size, fingerprint_hex);
        OPENSSL_free(der);
    }

    return computed;
}

/* ---------- private ---------- */

static bool handleSslError(TlsChannel *self, int32_t error)
{
    if (error == SSL_ERROR_WANT_READ) {
        self->want_write = false;
        return true;
    }
    if (error == SSL_ERROR_WANT_WRITE) {
        self->want_write = true;
        return true;
    }

    return false;
}
