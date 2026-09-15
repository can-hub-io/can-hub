#pragma once

#include <stdbool.h>

#include <picotls.h>

/*
 * AES-GCM and AES-ECB over the ARMv8 crypto extensions.
 *
 * AESE and AESMC are the AES round functions in silicon and PMULL is the
 * carry-less multiply GHASH needs, so the primitives are the instructions, not
 * an implementation of ours. What this module owns is the GHASH reduction, the
 * key schedule and the GCM framing, all three straight-line with no branch or
 * table lookup on secret data — the key schedule derives SubWord through AESE
 * rather than an S-box table for that reason — and all checked byte for byte
 * against minicrypto.
 *
 * The extensions are optional in ARMv8-A — a Raspberry Pi 4 has neither, a
 * Pi 5 and every server-class part have both — so availability is a runtime
 * question, answered once by TlsAesArmv8_IsSupported(). Without them the
 * profile falls back to CHACHA20-POLY1305, whose software implementation is
 * fast, rather than to cifra's constant-time AES, which is not.
 */

bool TlsAesArmv8_IsSupported(void);

extern ptls_aead_algorithm_t can_hub_armv8_aes128gcm;
extern ptls_aead_algorithm_t can_hub_armv8_aes256gcm;
extern ptls_cipher_algorithm_t can_hub_armv8_aes128ecb;
extern ptls_cipher_algorithm_t can_hub_armv8_aes256ecb;
