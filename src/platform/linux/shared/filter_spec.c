#include "platform/linux/shared/filter_spec.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "protocol/frame_message.h"

#define HEX_BASE 16

/* ---------- public ---------- */

bool FilterSpec_Parse(const char *text, CanFilter *filter)
{
    const char *separator = strchr(text, ':');
    char *id_end = NULL;
    char *mask_end = NULL;

    memset(filter, 0, sizeof(*filter));
    filter->can_id = (uint32_t)strtoul(text, &id_end, HEX_BASE);
    if (id_end == text) {
        return false;
    }

    if (separator == NULL) {
        filter->can_mask = FRAME_CAN_ID_MASK;
        return id_end == text + strlen(text);
    }
    if (id_end != separator) {
        return false;
    }

    filter->can_mask = (uint32_t)strtoul(separator + 1, &mask_end, HEX_BASE);
    return mask_end != separator + 1 && mask_end == text + strlen(text);
}

void FilterSpec_Format(const CanFilter *filter, char *out, size_t out_size)
{
    if (filter->can_mask == FRAME_CAN_ID_MASK) {
        snprintf(out, out_size, "%X", filter->can_id);
        return;
    }

    snprintf(out, out_size, "%X:%X", filter->can_id, filter->can_mask);
}
