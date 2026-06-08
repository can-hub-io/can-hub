#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "protocol/list_message.h"
#include "protocol/register_message.h"

#define INTERFACE_NAME_NAMESPACED_SIZE (REGISTER_AGENT_NAME_SIZE + REGISTER_INTERFACE_NAME_SIZE + 1)

bool InterfaceName_IsNamespaced(const char *target);
bool InterfaceName_Find(const ListReplyMessage *reply, const char *namespaced, uint32_t *interface_id);
