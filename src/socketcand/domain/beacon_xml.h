#pragma once

#include <stddef.h>

#include "socketcand/domain/interface_catalogue.h"

/*
 * Renders the socketcand discovery beacon: a CANBeacon XML document listing the
 * device name, the access URL and every exposed bus. Caller broadcasts the
 * result over UDP. Returns the document length, or 0 if it would not fit.
 * Freestanding, no stdio.
 */
size_t BeaconXml_Render(char *out, size_t out_size, const char *device_name, const char *url, const InterfaceCatalogue *catalogue);
