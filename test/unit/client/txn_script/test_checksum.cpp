#include <cest>

extern "C" {
#include "client/txn_script/checksum.h"
}

#define CHECK_INPUT "123456789"
#define CHECK_INPUT_SIZE 9

static const ChecksumModel modbus = { kCHECKSUM_KIND_CRC, 16, 0x8005, 0xFFFF, true, true, 0x0000 };
static const ChecksumModel crc8_autosar = { kCHECKSUM_KIND_CRC, 8, 0x2F, 0xFF, false, false, 0xFF };

static uint32_t checksumOf(const ChecksumModel *model, const char *text, uint32_t size);
static uint32_t checksumInTwoPieces(const ChecksumModel *model, const char *text, uint32_t size);

describe("checksum", []() {
    it("computes CRC-16/CCITT-FALSE", []() {
        expect(checksumOf(&CHECKSUM_CRC16, CHECK_INPUT, CHECK_INPUT_SIZE)).toBe((uint32_t)0x29B1);
    });

    it("computes CRC-32", []() {
        expect(checksumOf(&CHECKSUM_CRC32, CHECK_INPUT, CHECK_INPUT_SIZE)).toBe((uint32_t)0xCBF43926);
    });

    it("computes a reflected custom CRC such as MODBUS", []() {
        expect(checksumOf(&modbus, CHECK_INPUT, CHECK_INPUT_SIZE)).toBe((uint32_t)0x4B37);
    });

    it("computes an 8-bit custom CRC with a final xor", []() {
        expect(checksumOf(&crc8_autosar, CHECK_INPUT, CHECK_INPUT_SIZE)).toBe((uint32_t)0xDF);
    });

    it("truncates sums to their width", []() {
        expect(checksumOf(&CHECKSUM_SUM8, CHECK_INPUT, CHECK_INPUT_SIZE)).toBe((uint32_t)0xDD);
        expect(checksumOf(&CHECKSUM_SUM16, CHECK_INPUT, CHECK_INPUT_SIZE)).toBe((uint32_t)0x01DD);
    });

    it("gives the same result piece by piece as in one go", []() {
        uint32_t whole;
        uint32_t pieces;

        whole = checksumOf(&CHECKSUM_CRC32, CHECK_INPUT, CHECK_INPUT_SIZE);
        pieces = checksumInTwoPieces(&CHECKSUM_CRC32, CHECK_INPUT, CHECK_INPUT_SIZE);

        expect(pieces).toBe(whole);
    });

    it("refuses a width the comparison cannot hold", []() {
        ChecksumModel too_wide = { kCHECKSUM_KIND_CRC, 40, 1, 0, false, false, 0 };

        expect(ChecksumModel_IsValid(&too_wide)).toBe(false);
        expect(ChecksumModel_IsValid(&modbus)).toBe(true);
    });
});

static uint32_t checksumOf(const ChecksumModel *model, const char *text, uint32_t size)
{
    Checksum checksum;

    Checksum_Start(&checksum, model);
    Checksum_Update(&checksum, (const uint8_t *)text, size);
    return Checksum_Finish(&checksum);
}

static uint32_t checksumInTwoPieces(const ChecksumModel *model, const char *text, uint32_t size)
{
    Checksum checksum;
    uint32_t half = size / 2;

    Checksum_Start(&checksum, model);
    Checksum_Update(&checksum, (const uint8_t *)text, half);
    Checksum_Update(&checksum, (const uint8_t *)text + half, size - half);
    return Checksum_Finish(&checksum);
}
