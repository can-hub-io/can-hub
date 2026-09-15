#pragma once

#include <picotls.h>

/*
 * CHACHA20-POLY1305 with Monocypher's POLY1305 underneath: cifra's
 * authenticator, not its cipher, is what makes the minicrypto AEAD slow.
 * Everything else — nonce, padding, length encoding — stays picotls's.
 */

extern ptls_aead_algorithm_t can_hub_chacha20poly1305;
extern ptls_cipher_suite_t can_hub_chacha20poly1305sha256;

/*
 * AES for QUIC, which RFC 9001 fixes to AES-128-GCM for Initial packets
 * whatever the connection later negotiates. Resolved once and returned as
 * stable pointers: the ngtcp2 backend identifies header protection and the
 * AEAD usage limits by comparing the negotiated algorithm against exactly
 * these. Where no hardware AES exists they resolve to cifra, whose cost an
 * unauthenticated peer can make the hub pay — address validation's problem,
 * not the cipher's.
 */

ptls_aead_algorithm_t *TlsAead_Aes128Gcm(void);
ptls_cipher_algorithm_t *TlsAead_Aes128Ecb(void);

/* AES-256: never used for Initial packets, offered for policies that ask for a
   256-bit key. */

ptls_aead_algorithm_t *TlsAead_Aes256Gcm(void);
ptls_cipher_algorithm_t *TlsAead_Aes256Ecb(void);

/*
 * Which record layer the suites are for. picotls's two AES-NI engines are not
 * interchangeable: ptls_fusion_aes128gcm leaves do_encrypt_v as an upstream
 * assertion, and the TLS record layer encrypts through exactly that. On a
 * release build the assertion is compiled out and the peer sees a bad record
 * MAC, so the wrong engine here is a runtime failure, not a compile error.
 */

typedef enum ttls_transport_e {
    kTLS_TRANSPORT_STREAM,
    kTLS_TRANSPORT_QUIC,
    kTLS_TRANSPORT_MAX,
} TLS_TRANSPORT;

/*
 * The suites this build offers for a transport, most preferred first,
 * NULL-terminated. AES appears only where it is hardware-backed: the TLS 1.3
 * server picks from what the client offered, so offering an AES we cannot
 * afford lets any server pin us to cifra's.
 */

ptls_cipher_suite_t **TlsAead_CipherSuites(TLS_TRANSPORT transport);
