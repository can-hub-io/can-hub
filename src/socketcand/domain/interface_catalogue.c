#include "socketcand/domain/interface_catalogue.h"

#include <string.h>

static size_t fieldLength(const char *field, size_t field_size);
static bool composeName(char *destination, size_t destination_size, const ListReplyEntry *entry);

/* ---------- public ---------- */

void InterfaceCatalogue_Reset(InterfaceCatalogue *self)
{
    self->count = 0;
}

bool InterfaceCatalogue_AppendPage(InterfaceCatalogue *self, const ListReplyMessage *reply)
{
    uint8_t i;

    for(i=0; i<reply->count; i++) {
        if (self->count >= INTERFACE_CATALOGUE_MAX) {
            return false;
        }
        if (!composeName(self->entries[self->count].name, SOCKETCAND_BUS_NAME_SIZE, &reply->entries[i])) {
            continue;
        }
        self->entries[self->count].interface_id = reply->entries[i].interface_id;
        self->count++;
    }

    return true;
}

bool InterfaceCatalogue_FindByName(const InterfaceCatalogue *self, const char *name, uint32_t *interface_id)
{
    uint8_t i;

    for(i=0; i<self->count; i++) {
        if (strcmp(self->entries[i].name, name) == 0) {
            *interface_id = self->entries[i].interface_id;
            return true;
        }
    }

    return false;
}

uint8_t InterfaceCatalogue_Count(const InterfaceCatalogue *self)
{
    return self->count;
}

const CatalogueEntry *InterfaceCatalogue_At(const InterfaceCatalogue *self, uint8_t index)
{
    if (index >= self->count) {
        return NULL;
    }

    return &self->entries[index];
}

/* ---------- private ---------- */

static size_t fieldLength(const char *field, size_t field_size)
{
    const char *terminator = memchr(field, '\0', field_size);

    if (terminator == NULL) {
        return field_size;
    }

    return (size_t)(terminator - field);
}

static bool composeName(char *destination, size_t destination_size, const ListReplyEntry *entry)
{
    size_t agent_length = fieldLength(entry->agent_name, REGISTER_AGENT_NAME_SIZE);
    size_t interface_length = fieldLength(entry->interface_name, REGISTER_INTERFACE_NAME_SIZE);
    size_t position = 0;

    if (agent_length == 0 || interface_length == 0) {
        return false;
    }
    if (agent_length + 1 + interface_length + 1 > destination_size) {
        return false;
    }

    memcpy(destination + position, entry->agent_name, agent_length);
    position += agent_length;
    destination[position++] = '/';
    memcpy(destination + position, entry->interface_name, interface_length);
    position += interface_length;
    destination[position] = '\0';

    return true;
}
