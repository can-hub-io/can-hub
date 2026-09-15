#pragma once

#include <stdbool.h>

#include <picotls.h>

/*
 * AES-GCM and AES-ECB over the ARMv8 crypto extensions, in either execution
 * state — ARMv8-A defines AESE, AESMC and PMULL in AArch32 as well, which is
 * what a 32-bit userland on 64-bit silicon needs.
 *
 * The primitives are the instructions; this module owns the GHASH reduction,
 * the key schedule and the GCM framing, none of which branches or indexes a
 * table on secret data, and all of which are checked byte for byte against
 * minicrypto.
 *
 * The extensions are optional in ARMv8-A — a Raspberry Pi 4 has neither, a Pi 5
 * and every server part have both — so TlsAesArmv8_IsSupported() decides at
 * runtime. Without them the profile falls back to CHACHA20-POLY1305, not to
 * cifra's constant-time AES.
 */

bool TlsAesArmv8_IsSupported(void);

extern ptls_aead_algorithm_t can_hub_armv8_aes128gcm;
extern ptls_aead_algorithm_t can_hub_armv8_aes256gcm;
extern ptls_cipher_algorithm_t can_hub_armv8_aes128ecb;
extern ptls_cipher_algorithm_t can_hub_armv8_aes256ecb;
