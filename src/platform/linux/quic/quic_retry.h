#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <ngtcp2/ngtcp2.h>
#include <sys/socket.h>

#define QUIC_RETRY_SECRET_SIZE 32

/*
 * QUIC address validation for the hub (RFC 9000 §8.1.2): a Retry carries a
 * token bound to the client's address and original Destination Connection
 * ID, so a source that cannot receive at the address it claims never gets a
 * peer slot or a TLS session. Deciding to send one needs only the Initial's
 * cleartext header, never its decryption. The secret is minted per process,
 * so tokens do not outlive the hub.
 */
typedef struct {
    uint8_t secret[QUIC_RETRY_SECRET_SIZE];
} QuicRetry;

void QuicRetry_Init(QuicRetry *self);
bool QuicRetry_HasToken(const ngtcp2_pkt_hd *initial_header);
bool QuicRetry_Verify(
    const QuicRetry *self,
    const ngtcp2_pkt_hd *initial_header,
    const struct sockaddr_storage *address,
    socklen_t address_length,
    ngtcp2_cid *original_dcid
);
ngtcp2_ssize QuicRetry_Write(
    const QuicRetry *self,
    const ngtcp2_pkt_hd *initial_header,
    const struct sockaddr_storage *address,
    socklen_t address_length,
    uint8_t *packet,
    size_t packet_size
);
