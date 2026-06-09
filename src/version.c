#include "version.h"

#ifndef CAN_HUB_VERSION
#define CAN_HUB_VERSION "0.0.0-dev"
#endif

/* ---------- public ---------- */

const char *Version_String(void)
{
    return CAN_HUB_VERSION;
}
