#pragma once

/*
 * Minting a self-signed ED25519 identity is the one place the two TLS stacks
 * do not converge: OpenSSL does it through EVP and X509, wolfSSL only through
 * wolfcrypt. Each backend implements this and hands back the two PEM blobs;
 * everything around it — paths, permissions, fingerprints — stays common.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool TlsIdentityBackend_Generate(
    const char *common_name,
    uint8_t *certificate_pem,
    size_t certificate_pem_size,
    size_t *certificate_pem_length,
    uint8_t *key_pem,
    size_t key_pem_size,
    size_t *key_pem_length
);
