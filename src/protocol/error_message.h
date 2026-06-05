#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ERROR_BODY_SIZE 68
#define ERROR_DETAIL_SIZE 64

typedef struct {
    uint16_t code;
    char detail[ERROR_DETAIL_SIZE];
} ErrorMessage;

size_t ErrorMessage_Encode(const ErrorMessage *self, uint8_t *buffer, size_t buffer_size);
bool ErrorMessage_Decode(ErrorMessage *self, const uint8_t *payload, size_t payload_length);
