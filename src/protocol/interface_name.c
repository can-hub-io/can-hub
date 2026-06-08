#include "protocol/interface_name.h"

#include <stdio.h>
#include <string.h>

bool InterfaceName_IsNamespaced(const char *target)
{
    return strchr(target, '/') != NULL;
}

bool InterfaceName_Find(const ListReplyMessage *reply, const char *namespaced, uint32_t *interface_id)
{
    char candidate[INTERFACE_NAME_NAMESPACED_SIZE];
    uint8_t i;

    for(i=0; i<reply->count; i++) {
        snprintf(
            candidate,
            sizeof(candidate),
            "%s/%s",
            reply->entries[i].agent_name,
            reply->entries[i].interface_name
        );
        if (strcmp(candidate, namespaced) == 0) {
            *interface_id = reply->entries[i].interface_id;
            return true;
        }
    }

    return false;
}
