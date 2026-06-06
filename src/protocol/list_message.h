#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol/register_message.h"

#define LIST_BODY_SIZE 4
#define LIST_REPLY_ENTRIES_MAX 16
#define LIST_REPLY_ENTRY_SIZE 148
#define LIST_REPLY_FIXED_FIELDS_SIZE 4

#define LIST_REPLY_FLAG_MORE (1u << 0)

typedef struct {
    uint16_t offset;
} ListMessage;

typedef struct {
    uint32_t interface_id;
    char agent_name[REGISTER_AGENT_NAME_SIZE];
    char interface_name[REGISTER_INTERFACE_NAME_SIZE];
} ListReplyEntry;

typedef struct {
    uint8_t count;
    uint8_t flags;
    ListReplyEntry entries[LIST_REPLY_ENTRIES_MAX];
} ListReplyMessage;

size_t ListMessage_Encode(const ListMessage *self, uint8_t *buffer, size_t buffer_size);
bool ListMessage_Decode(ListMessage *self, const uint8_t *payload, size_t payload_length);

size_t ListReplyMessage_Encode(const ListReplyMessage *self, uint8_t *buffer, size_t buffer_size);
bool ListReplyMessage_Decode(ListReplyMessage *self, const uint8_t *payload, size_t payload_length);
