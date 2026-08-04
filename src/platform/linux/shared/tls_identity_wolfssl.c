#include "platform/linux/shared/tls_identity_backend.h"

#include <string.h>

#include <wolfssl/options.h>
#include <wolfssl/wolfcrypt/asn_public.h>
#include <wolfssl/wolfcrypt/ed25519.h>
#include <wolfssl/wolfcrypt/random.h>

/*
 * wolfSSL's OpenSSL compatibility layer covers the whole TLS surface but not
 * ED25519 key generation: EVP_PKEY_keygen refuses the curve, in 5.8.2 and in
 * 5.9.2 alike, so this is not a version to wait out. Certificate minting goes
 * through wolfcrypt directly instead, producing the same two PEM files the
 * OpenSSL path writes.
 */

#define CERTIFICATE_VALID_DAYS 3650
#define DER_BUFFER_SIZE 4096
#define PEM_BUFFER_SIZE 8192

static bool keyToPem(const ed25519_key *key, uint8_t *pem, size_t pem_size, size_t *pem_length);
static bool certificateToPem(
    Cert *certificate,
    ed25519_key *key,
    WC_RNG *rng,
    uint8_t *pem,
    size_t pem_size,
    size_t *pem_length
);

bool TlsIdentityBackend_Generate(
    const char *common_name,
    uint8_t *certificate_pem,
    size_t certificate_pem_size,
    size_t *certificate_pem_length,
    uint8_t *key_pem,
    size_t key_pem_size,
    size_t *key_pem_length
)
{
    WC_RNG rng;
    ed25519_key key;
    Cert certificate;
    bool generated = false;

    if (wc_InitRng(&rng) != 0) {
        return false;
    }
    if (wc_ed25519_init(&key) != 0) {
        wc_FreeRng(&rng);
        return false;
    }

    if (wc_ed25519_make_key(&rng, ED25519_KEY_SIZE, &key) == 0
        && wc_InitCert(&certificate) == 0) {
        certificate.daysValid = CERTIFICATE_VALID_DAYS;
        certificate.selfSigned = 1;
        certificate.sigType = CTC_ED25519;
        strncpy(certificate.subject.commonName, common_name, CTC_NAME_SIZE - 1);

        generated = keyToPem(&key, key_pem, key_pem_size, key_pem_length)
            && certificateToPem(&certificate, &key, &rng, certificate_pem, certificate_pem_size, certificate_pem_length);
    }

    wc_ed25519_free(&key);
    wc_FreeRng(&rng);

    return generated;
}

/* ---------- private ---------- */

static bool keyToPem(const ed25519_key *key, uint8_t *pem, size_t pem_size, size_t *pem_length)
{
    uint8_t der[DER_BUFFER_SIZE];
    int der_size;
    int written;

    der_size = wc_Ed25519PrivateKeyToDer((ed25519_key *)key, der, sizeof(der));
    if (der_size <= 0) {
        return false;
    }

    written = wc_DerToPem(der, (word32)der_size, pem, (word32)pem_size, PKCS8_PRIVATEKEY_TYPE);
    if (written <= 0) {
        return false;
    }
    *pem_length = (size_t)written;

    return true;
}

static bool certificateToPem(
    Cert *certificate,
    ed25519_key *key,
    WC_RNG *rng,
    uint8_t *pem,
    size_t pem_size,
    size_t *pem_length
)
{
    uint8_t der[DER_BUFFER_SIZE];
    int body_size;
    int der_size;
    int written;

    body_size = wc_MakeCert_ex(certificate, der, sizeof(der), ED25519_TYPE, key, rng);
    if (body_size <= 0) {
        return false;
    }

    der_size = wc_SignCert_ex(certificate->bodySz, certificate->sigType, der, sizeof(der), ED25519_TYPE, key, rng);
    if (der_size <= 0) {
        return false;
    }

    written = wc_DerToPem(der, (word32)der_size, pem, (word32)pem_size, CERT_TYPE);
    if (written <= 0) {
        return false;
    }
    *pem_length = (size_t)written;

    return true;
}
