#include "platform/linux/shared/tls_aes_armv8.h"

/* Compiled to nothing off aarch64, so the source lists stay one shape. */
#if defined(CAN_HUB_TLS_ARMV8_CRYPTO)

#include <arm_neon.h>
#include <string.h>
#include <asm/hwcap.h>
#include <sys/auxv.h>

#define AES_BLOCK_SIZE 16
#define AES_GCM_IV_SIZE 12
#define AES_GCM_TAG_SIZE 16
#define AES_MAX_ROUNDS 14
#define AES_ROUND_KEY_MAX (AES_MAX_ROUNDS + 1)

typedef struct {
    uint8x16_t round_keys[AES_ROUND_KEY_MAX];
    int rounds;
} AesKeySchedule;

typedef struct {
    ptls_aead_context_t super;
    AesKeySchedule schedule;
    uint8x16_t hash_key;
    uint8x16_t hash_key2;
    uint8x16_t hash_key3;
    uint8x16_t hash_key4;
    uint8_t static_iv[AES_GCM_IV_SIZE];
} ArmAesGcmContext;

typedef struct {
    ptls_cipher_context_t super;
    AesKeySchedule schedule;
} ArmAesEcbContext;

static const uint8_t substitution_box[256] = {
0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16 };

static uint32_t substituteWord(uint32_t word);
static uint32_t rotateWord(uint32_t word);
static void expandKey(AesKeySchedule *schedule, const uint8_t *key, size_t key_size);
static uint8x16_t encryptBlock(const AesKeySchedule *schedule, uint8x16_t state);
static uint8x16_t reverseBytes(uint8x16_t value);
static void multiplyWide(uint8x16_t a, uint8x16_t b, uint8x16_t *out_high, uint8x16_t *out_low);
static uint8x16_t reduceProduct(uint8x16_t high, uint8x16_t low);
static uint8x16_t multiplyField(uint8x16_t a, uint8x16_t b);
static void halveInGcmOrder(uint8_t value[AES_BLOCK_SIZE]);
static uint8x16_t counterBlock(const uint8_t iv[AES_GCM_IV_SIZE], uint32_t counter);
static uint8x16_t hashBytes(const ArmAesGcmContext *self, uint8x16_t accumulator, const uint8_t *data, size_t size);
static uint8x16_t hashLengths(const ArmAesGcmContext *self, uint8x16_t accumulator, size_t aad_size, size_t text_size);
static size_t counterCrypt(const ArmAesGcmContext *self, uint8_t *output, const uint8_t *input, size_t size,
                           const uint8_t iv[AES_GCM_IV_SIZE], uint32_t counter, size_t block_offset);
static void aeadDispose(ptls_aead_context_t *context);
static void aeadGetIv(ptls_aead_context_t *context, void *iv);
static void aeadSetIv(ptls_aead_context_t *context, const void *iv);
static void aeadEncryptVector(ptls_aead_context_t *context, void *output, ptls_iovec_t *input, size_t count,
                              uint64_t sequence, const void *aad, size_t aad_size);
static size_t aeadDecrypt(ptls_aead_context_t *context, void *output, const void *input, size_t size,
                          uint64_t sequence, const void *aad, size_t aad_size);
static int aeadSetup(ptls_aead_context_t *context, int is_encrypt, const void *key, const void *iv);
static void ecbDispose(ptls_cipher_context_t *context);
static void ecbTransform(ptls_cipher_context_t *context, void *output, const void *input, size_t size);
static int ecbSetup(ptls_cipher_context_t *context, int is_encrypt, const void *key);

/* ---------- public ---------- */

bool TlsAesArmv8_IsSupported(void)
{
    static int8_t supported = -1;

    if (supported < 0) {
        unsigned long capabilities = getauxval(AT_HWCAP);
        supported = ((capabilities & HWCAP_AES) != 0 && (capabilities & HWCAP_PMULL) != 0) ? 1 : 0;
    }

    return supported == 1;
}

ptls_aead_algorithm_t can_hub_armv8_aes128gcm = {
    "AES128-GCM",
    PTLS_AESGCM_CONFIDENTIALITY_LIMIT,
    PTLS_AESGCM_INTEGRITY_LIMIT,
    &can_hub_armv8_aes128ecb,
    &can_hub_armv8_aes128ecb,
    PTLS_AES128_KEY_SIZE,
    AES_GCM_IV_SIZE,
    AES_GCM_TAG_SIZE,
    { PTLS_TLS12_AESGCM_FIXED_IV_SIZE, PTLS_TLS12_AESGCM_RECORD_IV_SIZE },
    0,
    0,
    sizeof(ArmAesGcmContext),
    aeadSetup,
};

ptls_aead_algorithm_t can_hub_armv8_aes256gcm = {
    "AES256-GCM",
    PTLS_AESGCM_CONFIDENTIALITY_LIMIT,
    PTLS_AESGCM_INTEGRITY_LIMIT,
    &can_hub_armv8_aes256ecb,
    &can_hub_armv8_aes256ecb,
    PTLS_AES256_KEY_SIZE,
    AES_GCM_IV_SIZE,
    AES_GCM_TAG_SIZE,
    { PTLS_TLS12_AESGCM_FIXED_IV_SIZE, PTLS_TLS12_AESGCM_RECORD_IV_SIZE },
    0,
    0,
    sizeof(ArmAesGcmContext),
    aeadSetup,
};

ptls_cipher_algorithm_t can_hub_armv8_aes128ecb = {
    "AES128-ECB",
    PTLS_AES128_KEY_SIZE,
    PTLS_AES_BLOCK_SIZE,
    0,
    sizeof(ArmAesEcbContext),
    ecbSetup,
};

ptls_cipher_algorithm_t can_hub_armv8_aes256ecb = {
    "AES256-ECB",
    PTLS_AES256_KEY_SIZE,
    PTLS_AES_BLOCK_SIZE,
    0,
    sizeof(ArmAesEcbContext),
    ecbSetup,
};

/* ---------- private: key schedule and block ---------- */

static uint32_t substituteWord(uint32_t word)
{
    return ((uint32_t)substitution_box[word & 0xff])
         | ((uint32_t)substitution_box[(word >> 8) & 0xff] << 8)
         | ((uint32_t)substitution_box[(word >> 16) & 0xff] << 16)
         | ((uint32_t)substitution_box[(word >> 24) & 0xff] << 24);
}

static uint32_t rotateWord(uint32_t word)
{
    return (word >> 8) | (word << 24);
}

static void expandKey(AesKeySchedule *schedule, const uint8_t *key, size_t key_size)
{
    static const uint8_t round_constants[10] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36 };
    uint32_t words[4 * (AES_MAX_ROUNDS + 1)];
    int key_words = (int)(key_size / 4);
    int total;
    int i;

    schedule->rounds = key_words + 6;
    total = 4 * (schedule->rounds + 1);
    for (i = 0; i < key_words; i++) {
        memcpy(&words[i], key + i * 4, 4);
    }
    for (i = key_words; i < total; i++) {
        uint32_t previous = words[i - 1];

        if (i % key_words == 0) {
            previous = substituteWord(rotateWord(previous)) ^ (uint32_t)round_constants[i / key_words - 1];
        } else if (key_words > 6 && i % key_words == 4) {
            previous = substituteWord(previous);
        }
        words[i] = words[i - key_words] ^ previous;
    }
    for (i = 0; i <= schedule->rounds; i++) {
        schedule->round_keys[i] = vld1q_u8((const uint8_t *)&words[i * 4]);
    }
}

static uint8x16_t encryptBlock(const AesKeySchedule *schedule, uint8x16_t state)
{
    int i;

    for (i = 0; i < schedule->rounds - 1; i++) {
        state = vaesmcq_u8(vaeseq_u8(state, schedule->round_keys[i]));
    }

    return veorq_u8(vaeseq_u8(state, schedule->round_keys[schedule->rounds - 1]),
                    schedule->round_keys[schedule->rounds]);
}

/* ---------- private: ghash ---------- */

static uint8x16_t reverseBytes(uint8x16_t value)
{
    return vrev64q_u8(vextq_u8(value, value, 8));
}

static void multiplyWide(uint8x16_t a, uint8x16_t b, uint8x16_t *out_high, uint8x16_t *out_low)
{
    poly64x2_t left = vreinterpretq_p64_u8(a);
    poly64x2_t right = vreinterpretq_p64_u8(b);
    uint8x16_t low = vreinterpretq_u8_p128(vmull_p64(vgetq_lane_p64(left, 0), vgetq_lane_p64(right, 0)));
    uint8x16_t high = vreinterpretq_u8_p128(vmull_high_p64(left, right));
    uint8x16_t middle = vreinterpretq_u8_p128(vmull_p64(
        vgetq_lane_p64(left, 0) ^ vgetq_lane_p64(left, 1),
        vgetq_lane_p64(right, 0) ^ vgetq_lane_p64(right, 1)));
    uint8x16_t zero = vdupq_n_u8(0);

    middle = veorq_u8(middle, veorq_u8(low, high));
    *out_low = veorq_u8(low, vextq_u8(zero, middle, 8));
    *out_high = veorq_u8(high, vextq_u8(middle, zero, 8));
}

static uint8x16_t reduceProduct(uint8x16_t high, uint8x16_t low)
{
    uint8x16_t folded;

    folded = vreinterpretq_u8_p128(vmull_p64((poly64_t)vgetq_lane_u64(vreinterpretq_u64_u8(low), 0),
                                             (poly64_t)0xc200000000000000ull));
    low = veorq_u8(vextq_u8(low, low, 8), folded);
    folded = vreinterpretq_u8_p128(vmull_p64((poly64_t)vgetq_lane_u64(vreinterpretq_u64_u8(low), 0),
                                             (poly64_t)0xc200000000000000ull));
    low = veorq_u8(vextq_u8(low, low, 8), folded);

    return veorq_u8(high, low);
}

static uint8x16_t multiplyField(uint8x16_t a, uint8x16_t b)
{
    uint8x16_t high, low;

    multiplyWide(a, b, &high, &low);

    return reduceProduct(high, low);
}

/*
 * GCM numbers bits from the most significant end; PMULL does not. Reversing the
 * bytes of both operands and of the result gives the product GCM wants, up to
 * one factor of x: measured against a bit-by-bit reference, the reversed
 * product is exactly x times the true one. Halving the hash key once, at key
 * setup, cancels it for every block that follows.
 */
static void halveInGcmOrder(uint8_t value[AES_BLOCK_SIZE])
{
    int top = value[0] >> 7;
    int i;

    if (top) {
        value[0] ^= 0xe1;
    }
    for (i = 0; i < AES_BLOCK_SIZE - 1; i++) {
        value[i] = (uint8_t)((value[i] << 1) | (value[i + 1] >> 7));
    }
    value[AES_BLOCK_SIZE - 1] = (uint8_t)((value[AES_BLOCK_SIZE - 1] << 1) | top);
}

/*
 * Four blocks with one reduction: the products against H^4, H^3, H^2 and H have
 * no dependency between them, which takes the serial chain out of the loop.
 */
static uint8x16_t hashBytes(const ArmAesGcmContext *self, uint8x16_t accumulator, const uint8_t *data, size_t size)
{
    uint8_t partial[AES_BLOCK_SIZE];
    size_t offset = 0;

    while (offset + 4 * AES_BLOCK_SIZE <= size) {
        uint8x16_t high, low, term_high, term_low;

        multiplyWide(veorq_u8(accumulator, reverseBytes(vld1q_u8(data + offset))), self->hash_key4, &high, &low);
        multiplyWide(reverseBytes(vld1q_u8(data + offset + 16)), self->hash_key3, &term_high, &term_low);
        high = veorq_u8(high, term_high);
        low = veorq_u8(low, term_low);
        multiplyWide(reverseBytes(vld1q_u8(data + offset + 32)), self->hash_key2, &term_high, &term_low);
        high = veorq_u8(high, term_high);
        low = veorq_u8(low, term_low);
        multiplyWide(reverseBytes(vld1q_u8(data + offset + 48)), self->hash_key, &term_high, &term_low);
        high = veorq_u8(high, term_high);
        low = veorq_u8(low, term_low);
        accumulator = reduceProduct(high, low);
        offset += 4 * AES_BLOCK_SIZE;
    }
    for (; offset + AES_BLOCK_SIZE <= size; offset += AES_BLOCK_SIZE) {
        accumulator = multiplyField(veorq_u8(accumulator, reverseBytes(vld1q_u8(data + offset))), self->hash_key);
    }
    if (offset < size) {
        memset(partial, 0, sizeof(partial));
        memcpy(partial, data + offset, size - offset);
        accumulator = multiplyField(veorq_u8(accumulator, reverseBytes(vld1q_u8(partial))), self->hash_key);
    }

    return accumulator;
}

static uint8x16_t hashLengths(const ArmAesGcmContext *self, uint8x16_t accumulator, size_t aad_size, size_t text_size)
{
    uint8_t lengths[AES_BLOCK_SIZE];
    uint64_t aad_bits = (uint64_t)aad_size * 8;
    uint64_t text_bits = (uint64_t)text_size * 8;
    int i;

    for (i = 0; i < 8; i++) {
        lengths[i] = (uint8_t)(aad_bits >> ((7 - i) * 8));
        lengths[8 + i] = (uint8_t)(text_bits >> ((7 - i) * 8));
    }

    return multiplyField(veorq_u8(accumulator, reverseBytes(vld1q_u8(lengths))), self->hash_key);
}

/* ---------- private: counter mode ---------- */

static uint8x16_t counterBlock(const uint8_t iv[AES_GCM_IV_SIZE], uint32_t counter)
{
    uint8_t block[AES_BLOCK_SIZE];

    memcpy(block, iv, AES_GCM_IV_SIZE);
    block[12] = (uint8_t)(counter >> 24);
    block[13] = (uint8_t)(counter >> 16);
    block[14] = (uint8_t)(counter >> 8);
    block[15] = (uint8_t)counter;

    return vld1q_u8(block);
}

/*
 * Encrypts size bytes starting block_offset bytes into the keystream, and
 * returns the block offset the next call must resume from. Four counter blocks
 * run at a time where the offset allows it: the chains are independent, so the
 * several cycles of AESE latency stay covered.
 */
static size_t counterCrypt(const ArmAesGcmContext *self, uint8_t *output, const uint8_t *input, size_t size,
                           const uint8_t iv[AES_GCM_IV_SIZE], uint32_t counter, size_t block_offset)
{
    uint8_t keystream[AES_BLOCK_SIZE];
    size_t offset = 0;

    if (block_offset != 0) {
        size_t take = AES_BLOCK_SIZE - block_offset;
        size_t i;

        if (take > size) {
            take = size;
        }
        vst1q_u8(keystream, encryptBlock(&self->schedule, counterBlock(iv, counter)));
        for (i = 0; i < take; i++) {
            output[i] = input[i] ^ keystream[block_offset + i];
        }
        offset = take;
        block_offset += take;
        if (block_offset == AES_BLOCK_SIZE) {
            block_offset = 0;
            counter++;
        }
    }
    while (offset + 4 * AES_BLOCK_SIZE <= size) {
        uint8x16_t b0 = counterBlock(iv, counter);
        uint8x16_t b1 = counterBlock(iv, counter + 1);
        uint8x16_t b2 = counterBlock(iv, counter + 2);
        uint8x16_t b3 = counterBlock(iv, counter + 3);
        int round;

        for (round = 0; round < self->schedule.rounds - 1; round++) {
            uint8x16_t key = self->schedule.round_keys[round];

            b0 = vaesmcq_u8(vaeseq_u8(b0, key));
            b1 = vaesmcq_u8(vaeseq_u8(b1, key));
            b2 = vaesmcq_u8(vaeseq_u8(b2, key));
            b3 = vaesmcq_u8(vaeseq_u8(b3, key));
        }
        b0 = veorq_u8(vaeseq_u8(b0, self->schedule.round_keys[self->schedule.rounds - 1]),
                      self->schedule.round_keys[self->schedule.rounds]);
        b1 = veorq_u8(vaeseq_u8(b1, self->schedule.round_keys[self->schedule.rounds - 1]),
                      self->schedule.round_keys[self->schedule.rounds]);
        b2 = veorq_u8(vaeseq_u8(b2, self->schedule.round_keys[self->schedule.rounds - 1]),
                      self->schedule.round_keys[self->schedule.rounds]);
        b3 = veorq_u8(vaeseq_u8(b3, self->schedule.round_keys[self->schedule.rounds - 1]),
                      self->schedule.round_keys[self->schedule.rounds]);
        vst1q_u8(output + offset, veorq_u8(vld1q_u8(input + offset), b0));
        vst1q_u8(output + offset + 16, veorq_u8(vld1q_u8(input + offset + 16), b1));
        vst1q_u8(output + offset + 32, veorq_u8(vld1q_u8(input + offset + 32), b2));
        vst1q_u8(output + offset + 48, veorq_u8(vld1q_u8(input + offset + 48), b3));
        counter += 4;
        offset += 4 * AES_BLOCK_SIZE;
    }
    for (; offset + AES_BLOCK_SIZE <= size; offset += AES_BLOCK_SIZE) {
        vst1q_u8(output + offset, veorq_u8(vld1q_u8(input + offset),
                 encryptBlock(&self->schedule, counterBlock(iv, counter))));
        counter++;
    }
    if (offset < size) {
        size_t remaining = size - offset;
        size_t i;

        vst1q_u8(keystream, encryptBlock(&self->schedule, counterBlock(iv, counter)));
        for (i = 0; i < remaining; i++) {
            output[offset + i] = input[offset + i] ^ keystream[i];
        }
        block_offset = remaining;
    }

    return block_offset;
}

/* ---------- private: aead ---------- */

static void aeadDispose(ptls_aead_context_t *context)
{
    ArmAesGcmContext *self = (ArmAesGcmContext *)context;

    ptls_clear_memory(self, sizeof(*self));
}

static void aeadGetIv(ptls_aead_context_t *context, void *iv)
{
    ArmAesGcmContext *self = (ArmAesGcmContext *)context;

    memcpy(iv, self->static_iv, sizeof(self->static_iv));
}

static void aeadSetIv(ptls_aead_context_t *context, const void *iv)
{
    ArmAesGcmContext *self = (ArmAesGcmContext *)context;

    memcpy(self->static_iv, iv, sizeof(self->static_iv));
}

static void aeadEncryptVector(ptls_aead_context_t *context, void *output, ptls_iovec_t *input, size_t count,
                              uint64_t sequence, const void *aad, size_t aad_size)
{
    ArmAesGcmContext *self = (ArmAesGcmContext *)context;
    uint8_t iv[AES_GCM_IV_SIZE];
    uint8_t *destination = output;
    uint8x16_t accumulator = vdupq_n_u8(0);
    uint8x16_t tag_mask;
    size_t block_offset = 0;
    size_t written = 0;
    size_t i;

    ptls_aead__build_iv(self->super.algo, iv, self->static_iv, sequence);
    tag_mask = encryptBlock(&self->schedule, counterBlock(iv, 1));

    for (i = 0; i < count; i++) {
        block_offset = counterCrypt(self, destination + written, input[i].base, input[i].len, iv,
                                    2 + (uint32_t)(written / AES_BLOCK_SIZE), block_offset);
        written += input[i].len;
    }

    /* hashBytes zero-pads its own trailing partial block, which is exactly the
       padding GCM puts between the aad and the ciphertext */
    accumulator = hashBytes(self, accumulator, aad, aad_size);
    accumulator = hashBytes(self, accumulator, destination, written);
    accumulator = hashLengths(self, accumulator, aad_size, written);

    vst1q_u8(destination + written, veorq_u8(reverseBytes(accumulator), tag_mask));
}

static size_t aeadDecrypt(ptls_aead_context_t *context, void *output, const void *input, size_t size,
                          uint64_t sequence, const void *aad, size_t aad_size)
{
    ArmAesGcmContext *self = (ArmAesGcmContext *)context;
    const uint8_t *ciphertext = input;
    uint8_t iv[AES_GCM_IV_SIZE];
    uint8_t expected[AES_BLOCK_SIZE];
    uint8x16_t accumulator = vdupq_n_u8(0);
    uint8x16_t tag_mask;
    size_t text_size;
    uint8_t difference = 0;
    size_t i;

    if (size < AES_GCM_TAG_SIZE) {
        return SIZE_MAX;
    }
    text_size = size - AES_GCM_TAG_SIZE;

    ptls_aead__build_iv(self->super.algo, iv, self->static_iv, sequence);
    tag_mask = encryptBlock(&self->schedule, counterBlock(iv, 1));

    accumulator = hashBytes(self, accumulator, aad, aad_size);
    accumulator = hashBytes(self, accumulator, ciphertext, text_size);
    accumulator = hashLengths(self, accumulator, aad_size, text_size);
    vst1q_u8(expected, veorq_u8(reverseBytes(accumulator), tag_mask));

    for (i = 0; i < AES_GCM_TAG_SIZE; i++) {
        difference |= (uint8_t)(expected[i] ^ ciphertext[text_size + i]);
    }
    if (difference != 0) {
        return SIZE_MAX;
    }

    counterCrypt(self, output, ciphertext, text_size, iv, 2, 0);

    return text_size;
}

static int aeadSetup(ptls_aead_context_t *context, int is_encrypt, const void *key, const void *iv)
{
    ArmAesGcmContext *self = (ArmAesGcmContext *)context;
    uint8_t hash_key[AES_BLOCK_SIZE];

    (void)is_encrypt;

    memcpy(self->static_iv, iv, sizeof(self->static_iv));
    if (key == NULL) {
        return 0;
    }

    expandKey(&self->schedule, key, self->super.algo->key_size);
    vst1q_u8(hash_key, encryptBlock(&self->schedule, vdupq_n_u8(0)));
    halveInGcmOrder(hash_key);
    self->hash_key = reverseBytes(vld1q_u8(hash_key));
    self->hash_key2 = multiplyField(self->hash_key, self->hash_key);
    self->hash_key3 = multiplyField(self->hash_key2, self->hash_key);
    self->hash_key4 = multiplyField(self->hash_key2, self->hash_key2);
    ptls_clear_memory(hash_key, sizeof(hash_key));

    self->super.dispose_crypto = aeadDispose;
    self->super.do_get_iv = aeadGetIv;
    self->super.do_set_iv = aeadSetIv;
    self->super.do_encrypt = ptls_aead__do_encrypt;
    self->super.do_encrypt_v = aeadEncryptVector;
    self->super.do_decrypt = aeadDecrypt;

    return 0;
}

/* ---------- private: ecb, for quic header protection ---------- */

static void ecbDispose(ptls_cipher_context_t *context)
{
    ArmAesEcbContext *self = (ArmAesEcbContext *)context;

    ptls_clear_memory(&self->schedule, sizeof(self->schedule));
}

static void ecbTransform(ptls_cipher_context_t *context, void *output, const void *input, size_t size)
{
    ArmAesEcbContext *self = (ArmAesEcbContext *)context;
    size_t offset;

    for (offset = 0; offset + AES_BLOCK_SIZE <= size; offset += AES_BLOCK_SIZE) {
        vst1q_u8((uint8_t *)output + offset,
                 encryptBlock(&self->schedule, vld1q_u8((const uint8_t *)input + offset)));
    }
}

static int ecbSetup(ptls_cipher_context_t *context, int is_encrypt, const void *key)
{
    ArmAesEcbContext *self = (ArmAesEcbContext *)context;

    if (!is_encrypt) {
        return PTLS_ERROR_LIBRARY;
    }

    expandKey(&self->schedule, key, self->super.algo->key_size);
    self->super.do_dispose = ecbDispose;
    self->super.do_init = NULL;
    self->super.do_transform = ecbTransform;

    return 0;
}

#endif /* CAN_HUB_TLS_ARMV8_CRYPTO */
