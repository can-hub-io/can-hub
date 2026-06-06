#include "protocol/list_message.h"

#include <string.h>

#include "protocol/message_header.h"
#include "protocol/wire.h"

#define OFFSET_OFFSET 0
#define COUNT_OFFSET 0
#define FLAGS_OFFSET 1
#define ENTRIES_OFFSET 4

#define ENTRY_INTERFACE_ID_OFFSET 0
#define ENTRY_AGENT_NAME_OFFSET 4
#define ENTRY_INTERFACE_NAME_OFFSET 132

static bool areEntriesValid(const ListReplyMessage *message);
static void writeEntry(uint8_t *destination, const ListReplyEntry *entry);
static void readEntry(const uint8_t *source, ListReplyEntry *entry);

/* ---------- public ---------- */

size_t ListMessage_Encode(const ListMessage *self, uint8_t *buffer, size_t buffer_size)
{
    MessageHeader header;
    size_t total_size = MESSAGE_HEADER_SIZE + LIST_BODY_SIZE;
    uint8_t *body;

    if (buffer_size < total_size) {
        return 0;
    }

    header.type = kMESSAGE_TYPE_LIST;
    header.flags = 0;
    header.length = LIST_BODY_SIZE;
    MessageHeader_Encode(&header, buffer, buffer_size);

    body = buffer + MESSAGE_HEADER_SIZE;
    memset(body, 0, LIST_BODY_SIZE);
    Wire_WriteU16(body + OFFSET_OFFSET, self->offset);

    return total_size;
}

bool ListMessage_Decode(ListMessage *self, const uint8_t *payload, size_t payload_length)
{
    if (payload_length < LIST_BODY_SIZE) {
        return false;
    }

    self->offset = Wire_ReadU16(payload + OFFSET_OFFSET);

    return true;
}

size_t ListReplyMessage_Encode(const ListReplyMessage *self, uint8_t *buffer, size_t buffer_size)
{
    MessageHeader header;
    size_t body_size;
    size_t total_size;
    uint8_t *body;
    uint8_t i;

    if (self->count > LIST_REPLY_ENTRIES_MAX || !areEntriesValid(self)) {
        return 0;
    }

    body_size = LIST_REPLY_FIXED_FIELDS_SIZE + (size_t)self->count * LIST_REPLY_ENTRY_SIZE;
    total_size = MESSAGE_HEADER_SIZE + body_size;
    if (buffer_size < total_size) {
        return 0;
    }

    header.type = kMESSAGE_TYPE_LIST_REPLY;
    header.flags = 0;
    header.length = (uint16_t)body_size;
    MessageHeader_Encode(&header, buffer, buffer_size);

    body = buffer + MESSAGE_HEADER_SIZE;
    memset(body, 0, body_size);
    body[COUNT_OFFSET] = self->count;
    body[FLAGS_OFFSET] = self->flags;
    for(i=0; i<self->count; i++) {
        writeEntry(body + ENTRIES_OFFSET + i * LIST_REPLY_ENTRY_SIZE, &self->entries[i]);
    }

    return total_size;
}

bool ListReplyMessage_Decode(ListReplyMessage *self, const uint8_t *payload, size_t payload_length)
{
    uint8_t i;

    if (payload_length < LIST_REPLY_FIXED_FIELDS_SIZE) {
        return false;
    }

    self->count = payload[COUNT_OFFSET];
    self->flags = payload[FLAGS_OFFSET];
    if (self->count > LIST_REPLY_ENTRIES_MAX) {
        return false;
    }
    if (payload_length < LIST_REPLY_FIXED_FIELDS_SIZE + (size_t)self->count * LIST_REPLY_ENTRY_SIZE) {
        return false;
    }

    for(i=0; i<self->count; i++) {
        readEntry(payload + ENTRIES_OFFSET + i * LIST_REPLY_ENTRY_SIZE, &self->entries[i]);
    }

    return true;
}

/* ---------- private ---------- */

static bool areEntriesValid(const ListReplyMessage *message)
{
    uint8_t i;

    for(i=0; i<message->count; i++) {
        if (message->entries[i].agent_name[REGISTER_AGENT_NAME_SIZE - 1] != '\0') {
            return false;
        }
        if (message->entries[i].interface_name[REGISTER_INTERFACE_NAME_SIZE - 1] != '\0') {
            return false;
        }
    }

    return true;
}

static void writeEntry(uint8_t *destination, const ListReplyEntry *entry)
{
    Wire_WriteU32(destination + ENTRY_INTERFACE_ID_OFFSET, entry->interface_id);
    memcpy(destination + ENTRY_AGENT_NAME_OFFSET, entry->agent_name, strlen(entry->agent_name));
    memcpy(destination + ENTRY_INTERFACE_NAME_OFFSET, entry->interface_name, strlen(entry->interface_name));
}

static void readEntry(const uint8_t *source, ListReplyEntry *entry)
{
    entry->interface_id = Wire_ReadU32(source + ENTRY_INTERFACE_ID_OFFSET);
    memcpy(entry->agent_name, source + ENTRY_AGENT_NAME_OFFSET, REGISTER_AGENT_NAME_SIZE);
    entry->agent_name[REGISTER_AGENT_NAME_SIZE - 1] = '\0';
    memcpy(entry->interface_name, source + ENTRY_INTERFACE_NAME_OFFSET, REGISTER_INTERFACE_NAME_SIZE);
    entry->interface_name[REGISTER_INTERFACE_NAME_SIZE - 1] = '\0';
}
