#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "protocol/list_message.h"
#include "socketcand/domain/socketcand_codec.h"

/*
 * Cache of the hub's interface catalogue, filled from paginated LIST_REPLY
 * pages. Maps the namespaced bus name "agent_name/interface_name" to the
 * hub-assigned interface_id and enumerates entries for the discovery beacon.
 * Freestanding, fixed capacity.
 */

#define INTERFACE_CATALOGUE_MAX 128

typedef struct {
    uint32_t interface_id;
    char name[SOCKETCAND_BUS_NAME_SIZE];
} CatalogueEntry;

typedef struct {
    CatalogueEntry entries[INTERFACE_CATALOGUE_MAX];
    uint8_t count;
} InterfaceCatalogue;

void InterfaceCatalogue_Reset(InterfaceCatalogue *self);
bool InterfaceCatalogue_AppendPage(InterfaceCatalogue *self, const ListReplyMessage *reply);
bool InterfaceCatalogue_FindByName(const InterfaceCatalogue *self, const char *name, uint32_t *interface_id);
uint8_t InterfaceCatalogue_Count(const InterfaceCatalogue *self);
const CatalogueEntry *InterfaceCatalogue_At(const InterfaceCatalogue *self, uint8_t index);
