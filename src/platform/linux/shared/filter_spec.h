#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "protocol/subscribe_message.h"

/*
 * id/mask filter syntax shared by can-hub-client dump and can-hub-cli bridge:
 * <id>[:<mask>], both hex; without a mask the id must match exactly.
 */
bool FilterSpec_Parse(const char *text, CanFilter *filter);
void FilterSpec_Format(const CanFilter *filter, char *out, size_t out_size);
