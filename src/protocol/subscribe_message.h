#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SUBSCRIBE_FILTERS_MAX 16
#define SUBSCRIBE_FIXED_FIELDS_SIZE 4
#define CAN_FILTER_SIZE 8

typedef struct {
    uint32_t can_id;
    uint32_t can_mask;
} CanFilter;

typedef struct {
    uint8_t channel;
    uint8_t filter_count;
    CanFilter filters[SUBSCRIBE_FILTERS_MAX];
} SubscribeMessage;

size_t SubscribeMessage_Encode(const SubscribeMessage *self, uint8_t *buffer, size_t buffer_size);
bool SubscribeMessage_Decode(SubscribeMessage *self, const uint8_t *payload, size_t payload_length);
