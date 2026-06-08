#include "socketcand/domain/beacon_xml.h"

#include <string.h>

static bool append(char *out, size_t out_size, size_t *position, const char *text);

/* ---------- public ---------- */

size_t BeaconXml_Render(char *out, size_t out_size, const char *device_name, const char *url, const InterfaceCatalogue *catalogue)
{
    size_t position = 0;
    uint8_t i;

    if (!append(out, out_size, &position, "<CANBeacon name=\"")) {
        return 0;
    }
    if (!append(out, out_size, &position, device_name)) {
        return 0;
    }
    if (!append(out, out_size, &position, "\" type=\"SocketCAN\" description=\"can-hub\">\n")) {
        return 0;
    }
    if (!append(out, out_size, &position, "<URL>")) {
        return 0;
    }
    if (!append(out, out_size, &position, url)) {
        return 0;
    }
    if (!append(out, out_size, &position, "</URL>\n")) {
        return 0;
    }

    for(i=0; i<InterfaceCatalogue_Count(catalogue); i++) {
        const CatalogueEntry *entry = InterfaceCatalogue_At(catalogue, i);
        if (!append(out, out_size, &position, "<Bus name=\"")) {
            return 0;
        }
        if (!append(out, out_size, &position, entry->name)) {
            return 0;
        }
        if (!append(out, out_size, &position, "\"/>\n")) {
            return 0;
        }
    }

    if (!append(out, out_size, &position, "</CANBeacon>")) {
        return 0;
    }
    out[position] = '\0';

    return position;
}

/* ---------- private ---------- */

static bool append(char *out, size_t out_size, size_t *position, const char *text)
{
    size_t length = strlen(text);

    if (*position + length >= out_size) {
        return false;
    }
    memcpy(out + *position, text, length);
    *position += length;

    return true;
}
