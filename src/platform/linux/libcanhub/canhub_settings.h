#pragma once

#include <stdint.h>

#include "canhub.h"
#include "protocol/ifconfig_message.h"

/*
 * The libcanhub view of interface settings and the wire one, for both the
 * linux and the windows library. The public bits are the wire bits, so the
 * conversion only moves fields; the result maps a CONFIGURE status onto the
 * library's error codes.
 */
void CanHubSettings_ToWire(const CanHubInterfaceSettings *public_settings, InterfaceSettings *settings);
void CanHubSettings_FromWire(const InterfaceSettings *settings, CanHubInterfaceSettings *public_settings);
int32_t CanHubSettings_Result(uint8_t configure_status);
