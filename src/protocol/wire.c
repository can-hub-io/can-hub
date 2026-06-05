#include "protocol/wire.h"

#define BYTE_MASK 0xFF
#define BITS_PER_BYTE 8

/* ---------- public ---------- */

void Wire_WriteU16(uint8_t *buffer, uint16_t value)
{
    buffer[0] = (uint8_t)(value & BYTE_MASK);
    buffer[1] = (uint8_t)((value >> BITS_PER_BYTE) & BYTE_MASK);
}

void Wire_WriteU32(uint8_t *buffer, uint32_t value)
{
    Wire_WriteU16(buffer, (uint16_t)(value & 0xFFFF));
    Wire_WriteU16(buffer + 2, (uint16_t)(value >> 16));
}

void Wire_WriteU64(uint8_t *buffer, uint64_t value)
{
    Wire_WriteU32(buffer, (uint32_t)(value & 0xFFFFFFFF));
    Wire_WriteU32(buffer + 4, (uint32_t)(value >> 32));
}

uint16_t Wire_ReadU16(const uint8_t *buffer)
{
    return (uint16_t)(buffer[0] | ((uint16_t)buffer[1] << BITS_PER_BYTE));
}

uint32_t Wire_ReadU32(const uint8_t *buffer)
{
    return Wire_ReadU16(buffer) | ((uint32_t)Wire_ReadU16(buffer + 2) << 16);
}

uint64_t Wire_ReadU64(const uint8_t *buffer)
{
    return Wire_ReadU32(buffer) | ((uint64_t)Wire_ReadU32(buffer + 4) << 32);
}
