#include "client/txn_script/checksum.h"

#define BITS_PER_BYTE 8
#define BYTE_TOP_BIT 0x80
#define CRC16_CCITT_POLYNOMIAL 0x1021
#define CRC16_CCITT_INITIAL 0xFFFF
#define CRC32_POLYNOMIAL 0x04C11DB7
#define CRC32_INITIAL 0xFFFFFFFF
#define CRC32_FINAL_XOR 0xFFFFFFFF

const ChecksumModel CHECKSUM_SUM8 = { kCHECKSUM_KIND_SUM, 8, 0, 0, false, false, 0 };
const ChecksumModel CHECKSUM_SUM16 = { kCHECKSUM_KIND_SUM, 16, 0, 0, false, false, 0 };
const ChecksumModel CHECKSUM_SUM32 = { kCHECKSUM_KIND_SUM, 32, 0, 0, false, false, 0 };
const ChecksumModel CHECKSUM_CRC16 = { kCHECKSUM_KIND_CRC, 16, CRC16_CCITT_POLYNOMIAL, CRC16_CCITT_INITIAL, false, false, 0 };
const ChecksumModel CHECKSUM_CRC32 = {
    kCHECKSUM_KIND_CRC, 32, CRC32_POLYNOMIAL, CRC32_INITIAL, true, true, CRC32_FINAL_XOR
};

static uint32_t widthMask(uint8_t width);
static uint32_t reflect(uint32_t value, uint8_t width);
static uint32_t crcByte(const ChecksumModel *model, uint32_t value, uint8_t byte);

/* ---------- public ---------- */

bool ChecksumModel_IsValid(const ChecksumModel *model)
{
    if (model->kind >= kCHECKSUM_KIND_MAX) {
        return false;
    }
    return model->width >= CHECKSUM_WIDTH_MIN && model->width <= CHECKSUM_WIDTH_MAX;
}

void Checksum_Start(Checksum *self, const ChecksumModel *model)
{
    self->model = *model;
    self->value = model->kind == kCHECKSUM_KIND_CRC ? model->initial & widthMask(model->width) : 0;
}

void Checksum_Update(Checksum *self, const uint8_t *data, uint32_t size)
{
    uint32_t i;

    for(i=0; i<size; i++) {
        if (self->model.kind == kCHECKSUM_KIND_SUM) {
            self->value = (self->value + data[i]) & widthMask(self->model.width);
        } else {
            self->value = crcByte(&self->model, self->value, data[i]);
        }
    }
}

uint32_t Checksum_Finish(const Checksum *self)
{
    uint32_t value = self->value;

    if (self->model.kind == kCHECKSUM_KIND_SUM) {
        return value;
    }
    if (self->model.reflect_output) {
        value = reflect(value, self->model.width);
    }
    return (value ^ self->model.final_xor) & widthMask(self->model.width);
}

/* ---------- private ---------- */

static uint32_t widthMask(uint8_t width)
{
    if (width >= CHECKSUM_WIDTH_MAX) {
        return UINT32_MAX;
    }
    return ((uint32_t)1 << width) - 1;
}

static uint32_t reflect(uint32_t value, uint8_t width)
{
    uint32_t reflected = 0;
    uint8_t i;

    for(i=0; i<width; i++) {
        if (value & ((uint32_t)1 << i)) {
            reflected |= (uint32_t)1 << (width - 1 - i);
        }
    }
    return reflected;
}

static uint32_t crcByte(const ChecksumModel *model, uint32_t value, uint8_t byte)
{
    uint32_t top_bit = (uint32_t)1 << (model->width - 1);
    uint8_t input = model->reflect_input ? (uint8_t)reflect(byte, BITS_PER_BYTE) : byte;
    uint8_t i;

    value ^= (uint32_t)input << (model->width - BITS_PER_BYTE);
    for(i=0; i<BITS_PER_BYTE; i++) {
        if (value & top_bit) {
            value = (value << 1) ^ model->polynomial;
        } else {
            value <<= 1;
        }
    }
    return value & widthMask(model->width);
}
