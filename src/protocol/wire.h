#pragma once

#include <stddef.h>
#include <stdint.h>

void Wire_WriteU16(uint8_t *buffer, uint16_t value);
void Wire_WriteU32(uint8_t *buffer, uint32_t value);
void Wire_WriteU64(uint8_t *buffer, uint64_t value);
uint16_t Wire_ReadU16(const uint8_t *buffer);
uint32_t Wire_ReadU32(const uint8_t *buffer);
uint64_t Wire_ReadU64(const uint8_t *buffer);
