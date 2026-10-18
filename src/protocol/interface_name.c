#include "protocol/interface_name.h"

#include <string.h>

#define INTERFACE_NAME_SEPARATOR '/'

static bool namesEntry(const char *namespaced, const ListReplyEntry *entry);

bool InterfaceName_IsNamespaced(const char *target)
{
    return strchr(target, INTERFACE_NAME_SEPARATOR) != NULL;
}

bool InterfaceName_Find(const ListReplyMessage *reply, const char *namespaced, uint32_t *interface_id)
{
    uint8_t i;

    for(i=0; i<reply->count; i++) {
        if (namesEntry(namespaced, &reply->entries[i])) {
            *interface_id = reply->entries[i].interface_id;
            return true;
        }
    }

    return false;
}

static bool namesEntry(const char *namespaced, const ListReplyEntry *entry)
{
    size_t agent_length = strlen(entry->agent_name);

    if (strncmp(namespaced, entry->agent_name, agent_length) != 0) {
        return false;
    }
    if (namespaced[agent_length] != INTERFACE_NAME_SEPARATOR) {
        return false;
    }

    return strcmp(namespaced + agent_length + 1, entry->interface_name) == 0;
}

