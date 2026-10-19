#include "protocol/interface_name.h"

#include <string.h>

#define INTERFACE_NAME_SEPARATOR '/'

static bool namesEntry(const char *namespaced, const ListReplyEntry *entry);

bool InterfaceName_IsNamespaced(const char *target)
{
    return strchr(target, INTERFACE_NAME_SEPARATOR) != NULL;
}

bool InterfaceName_Split(const char *namespaced, char *agent_name, char *interface_name)
{
    const char *separator = strchr(namespaced, INTERFACE_NAME_SEPARATOR);
    size_t agent_length;
    size_t interface_length;

    if (separator == NULL) {
        return false;
    }

    agent_length = (size_t)(separator - namespaced);
    interface_length = strlen(separator + 1);
    if (agent_length == 0 || agent_length >= REGISTER_AGENT_NAME_SIZE) {
        return false;
    }
    if (interface_length == 0 || interface_length >= REGISTER_INTERFACE_NAME_SIZE) {
        return false;
    }

    memcpy(agent_name, namespaced, agent_length);
    agent_name[agent_length] = '\0';
    memcpy(interface_name, separator + 1, interface_length + 1);

    return true;
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

