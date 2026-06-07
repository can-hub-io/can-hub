#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ERROR_BODY_SIZE 68
#define ERROR_DETAIL_SIZE 64

typedef enum terror_code_e {
    kERROR_CODE_MALFORMED = 1,
    kERROR_CODE_ROLE_REJECTED = 2,
    kERROR_CODE_HUB_FULL = 3,
    kERROR_CODE_HELLO_TIMEOUT = 4,
    kERROR_CODE_KICKED = 5,
    kERROR_CODE_MAX,
} TERROR_CODE;

typedef struct {
    uint16_t code;
    char detail[ERROR_DETAIL_SIZE];
} ErrorMessage;

size_t ErrorMessage_Encode(const ErrorMessage *self, uint8_t *buffer, size_t buffer_size);
bool ErrorMessage_Decode(ErrorMessage *self, const uint8_t *payload, size_t payload_length);
