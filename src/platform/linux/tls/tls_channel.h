#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openssl/ssl.h>

#include "platform/linux/shared/message_framer.h"

#define TLS_CHANNEL_TX_BACKLOG_SIZE 8192
#define TLS_CHANNEL_NO_SOCKET (-1)

/*
 * One TLS-protected TCP stream carrying protocol messages: an OpenSSL
 * session driven over a nonblocking fd, RX reassembly via the framer, TX
 * backlog so partial record writes never corrupt the stream. The owner
 * creates the session (context, verification) and binds it together with
 * the fd; Pump drives the handshake until IsEstablished. Closing frees the
 * session, the fd stays owned by the caller.
 */
typedef enum ttls_channel_state_e {
    kTLS_CHANNEL_STATE_UNBOUND = 0,
    kTLS_CHANNEL_STATE_HANDSHAKING,
    kTLS_CHANNEL_STATE_ESTABLISHED,
    kTLS_CHANNEL_STATE_MAX,
} TTLS_CHANNEL_STATE;

typedef struct {
    int32_t fd;
    SSL *ssl;
    uint8_t state;
    bool want_write;
    MessageFramer framer;
    uint8_t tx_backlog[TLS_CHANNEL_TX_BACKLOG_SIZE];
    size_t tx_used;
} TlsChannel;

void TlsChannel_Reset(TlsChannel *self);
void TlsChannel_Bind(TlsChannel *self, int32_t fd, SSL *ssl);
void TlsChannel_Close(TlsChannel *self);
bool TlsChannel_IsBound(const TlsChannel *self);
bool TlsChannel_IsEstablished(const TlsChannel *self);
bool TlsChannel_Pump(TlsChannel *self);
bool TlsChannel_Receive(TlsChannel *self);
size_t TlsChannel_FreeTxSpace(const TlsChannel *self);
bool TlsChannel_Queue(TlsChannel *self, const uint8_t *data, size_t size);
bool TlsChannel_Flush(TlsChannel *self);
bool TlsChannel_WantsWrite(const TlsChannel *self);
bool TlsChannel_PeerFingerprint(const TlsChannel *self, char *fingerprint_hex);
