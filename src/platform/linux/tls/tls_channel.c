#include "platform/linux/tls/tls_channel.h"

#include <string.h>

#include "platform/linux/shared/tls_identity.h"

#define READ_CHUNK_SIZE 2048

static bool isRetryResult(ssize_t result);

/* ---------- public ---------- */

void TlsChannel_Reset(TlsChannel *self)
{
    memset(self, 0, sizeof(*self));
    self->fd = TLS_CHANNEL_NO_SOCKET;
    MessageFramer_Reset(&self->framer);
}

void TlsChannel_Bind(TlsChannel *self, int32_t fd, gnutls_session_t session)
{
    TlsChannel_Reset(self);
    self->fd = fd;
    self->session = session;
    self->state = kTLS_CHANNEL_STATE_HANDSHAKING;
    gnutls_transport_set_int(session, fd);
}

void TlsChannel_Close(TlsChannel *self)
{
    if (self->session != NULL) {
        gnutls_deinit(self->session);
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

    result = gnutls_handshake(self->session);
    if (result == 0) {
        self->state = kTLS_CHANNEL_STATE_ESTABLISHED;
        return true;
    }
    if (isRetryResult(result)) {
        return true;
    }

    return false;
}

bool TlsChannel_Receive(TlsChannel *self)
{
    uint8_t chunk[READ_CHUNK_SIZE];
    ssize_t bytes_received;

    if (self->state != kTLS_CHANNEL_STATE_ESTABLISHED) {
        return true;
    }

    for (;;) {
        bytes_received = gnutls_record_recv(self->session, chunk, sizeof(chunk));
        if (isRetryResult(bytes_received)) {
            return true;
        }
        if (bytes_received <= 0) {
            return false;
        }

        if (!MessageFramer_Push(&self->framer, chunk, (size_t)bytes_received)) {
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
    ssize_t bytes_sent;

    if (self->state != kTLS_CHANNEL_STATE_ESTABLISHED) {
        return true;
    }

    while (self->tx_used > 0) {
        if (self->record_pending) {
            bytes_sent = gnutls_record_send(self->session, NULL, 0);
        } else {
            bytes_sent = gnutls_record_send(self->session, self->tx_backlog, self->tx_used);
        }
        if (isRetryResult(bytes_sent)) {
            self->record_pending = true;
            return true;
        }
        if (bytes_sent <= 0) {
            return false;
        }

        self->record_pending = false;
        memmove(self->tx_backlog, self->tx_backlog + bytes_sent, self->tx_used - (size_t)bytes_sent);
        self->tx_used -= (size_t)bytes_sent;
    }

    return true;
}

bool TlsChannel_WantsWrite(const TlsChannel *self)
{
    if (self->state == kTLS_CHANNEL_STATE_HANDSHAKING) {
        return gnutls_record_get_direction(self->session) == 1;
    }

    return self->tx_used > 0;
}

bool TlsChannel_PeerFingerprint(const TlsChannel *self, char *fingerprint_hex)
{
    const gnutls_datum_t *certificates;
    unsigned int certificate_count;

    certificates = gnutls_certificate_get_peers(self->session, &certificate_count);
    if (certificates == NULL || certificate_count == 0) {
        return false;
    }

    return TlsIdentity_FingerprintOfDer(&certificates[0], fingerprint_hex);
}

/* ---------- private ---------- */

static bool isRetryResult(ssize_t result)
{
    return result == GNUTLS_E_AGAIN || result == GNUTLS_E_INTERRUPTED;
}
