#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CHECKSUM_WIDTH_MIN 8
#define CHECKSUM_WIDTH_MAX 32

/*
 * The checks a transaction script can compute over data: a sum of the bytes
 * truncated to its width, or a CRC of the standard (Rocksoft) model, the one
 * every CRC catalogue describes with width, polynomial, initial value, input
 * and output reflection and final xor. Updated piece by piece, so a file can
 * be checked as it streams.
 */
typedef enum tchecksum_kind_e {
    kCHECKSUM_KIND_SUM = 0,
    kCHECKSUM_KIND_CRC = 1,
    kCHECKSUM_KIND_MAX,
} TCHECKSUM_KIND;

typedef struct {
    uint8_t kind;
    uint8_t width;
    uint32_t polynomial;
    uint32_t initial;
    bool reflect_input;
    bool reflect_output;
    uint32_t final_xor;
} ChecksumModel;

typedef struct {
    ChecksumModel model;
    uint32_t value;
} Checksum;

extern const ChecksumModel CHECKSUM_SUM8;
extern const ChecksumModel CHECKSUM_SUM16;
extern const ChecksumModel CHECKSUM_SUM32;
extern const ChecksumModel CHECKSUM_CRC16;
extern const ChecksumModel CHECKSUM_CRC32;

bool ChecksumModel_IsValid(const ChecksumModel *model);
void Checksum_Start(Checksum *self, const ChecksumModel *model);
void Checksum_Update(Checksum *self, const uint8_t *data, uint32_t size);
uint32_t Checksum_Finish(const Checksum *self);
