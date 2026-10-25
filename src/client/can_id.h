#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "canhub.h"

/*
 * A libcanhub CAN id against the wire can_id: 11 or 29 bits, the extended
 * flag carried next to the value in the API and as bit 31 on the wire.
 */
uint32_t CanId_Width(CanHubCanId id);
bool CanId_IsValid(CanHubCanId id);
bool CanId_AreIgnoredBitsValid(CanHubCanId id, uint32_t ignored_bits);
uint32_t CanId_ToWire(CanHubCanId id);
