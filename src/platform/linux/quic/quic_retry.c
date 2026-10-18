#include "platform/linux/quic/quic_retry.h"

#include <ngtcp2/ngtcp2_crypto.h>
#include <picotls/minicrypto.h>

#include "platform/linux/clock/clock.h"
#include "platform/linux/quic/quic_connection.h"

#define RETRY_TOKEN_TIMEOUT (10 * NGTCP2_SECONDS)

/* ---------- public ---------- */

void QuicRetry_Init(QuicRetry *self)
{
    ptls_minicrypto_random_bytes(self->secret, sizeof(self->secret));
}

bool QuicRetry_HasToken(const ngtcp2_pkt_hd *initial_header)
{
    return initial_header->tokenlen > 0 && initial_header->token[0] == NGTCP2_CRYPTO_TOKEN_MAGIC_RETRY2;
}

bool QuicRetry_Verify(
    const QuicRetry *self,
    const ngtcp2_pkt_hd *initial_header,
    const struct sockaddr_storage *address,
    socklen_t address_length,
    ngtcp2_cid *original_dcid
)
{
    int32_t result = ngtcp2_crypto_verify_retry_token2(
        original_dcid,
        initial_header->token,
        initial_header->tokenlen,
        self->secret,
        sizeof(self->secret),
        initial_header->version,
        (const ngtcp2_sockaddr *)address,
        address_length,
        &initial_header->dcid,
        RETRY_TOKEN_TIMEOUT,
        Clock_MonotonicNs()
    );

    return result == 0;
}

ngtcp2_ssize QuicRetry_Write(
    const QuicRetry *self,
    const ngtcp2_pkt_hd *initial_header,
    const struct sockaddr_storage *address,
    socklen_t address_length,
    uint8_t *packet,
    size_t packet_size
)
{
    uint8_t token[NGTCP2_CRYPTO_MAX_RETRY_TOKENLEN2];
    ngtcp2_cid retry_scid;
    ngtcp2_ssize token_length;

    ptls_minicrypto_random_bytes(retry_scid.data, QUIC_CONNECTION_CID_LENGTH);
    retry_scid.datalen = QUIC_CONNECTION_CID_LENGTH;

    token_length = ngtcp2_crypto_generate_retry_token2(
        token,
        self->secret,
        sizeof(self->secret),
        initial_header->version,
        (const ngtcp2_sockaddr *)address,
        address_length,
        &retry_scid,
        &initial_header->dcid,
        Clock_MonotonicNs()
    );
    if (token_length < 0) {
        return token_length;
    }

    return ngtcp2_crypto_write_retry(
        packet,
        packet_size,
        initial_header->version,
        &initial_header->scid,
        &retry_scid,
        &initial_header->dcid,
        token,
        (size_t)token_length
    );
}
