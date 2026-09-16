#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <picotls.h>

#include "platform/linux/shared/tls_identity.h"

/*
 * Peer certificate state for the picotls verify_certificate callbacks. The
 * fingerprint is taken over the raw DER picotls hands over, so no X.509 object
 * is built. One per session, doubling as the verify_sign context.
 *
 * The session is reached differently per transport — TLS-over-TCP owns the
 * picotls data pointer, QUIC must leave it to ngtcp2 — so a resolver supplied
 * at attach time is what keeps one pair of callbacks serving both.
 */

typedef struct {
    uint8_t public_key[TLS_IDENTITY_PUBLIC_KEY_SIZE];
    char fingerprint[TLS_IDENTITY_FINGERPRINT_HEX_SIZE];
    bool loaded;
} TlsPeerCertificate;

typedef TlsPeerCertificate *(*TlsPeerResolver)(ptls_t *tls);

void TlsPeerCertificate_Reset(TlsPeerCertificate *self);
bool TlsPeerCertificate_Accept(TlsPeerCertificate *self, ptls_iovec_t *certificates, size_t count);
TlsPeerCertificate *TlsPeerCertificate_FromDataPointer(ptls_t *tls);
int32_t TlsPeerCertificate_VerifySignature(
    void *verify_context,
    uint16_t algorithm,
    ptls_iovec_t data,
    ptls_iovec_t signature
);
