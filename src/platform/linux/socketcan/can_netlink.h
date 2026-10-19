#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "protocol/ifconfig_message.h"

/*
 * rtnetlink helpers to reconfigure a CAN interface from the agent. Apply sets
 * every requested field in one RTM_NEWLINK; bit timing, control modes and
 * restart-ms need the link down, so it is brought down around them and left
 * as requested (or as it was) afterwards. On failure the kernel's reason is
 * written into detail (IFCONFIG_DETAIL_SIZE bytes) and the link is restored.
 * Changing anything needs CAP_NET_ADMIN.
 */
bool CanNetlink_Apply(const char *interface_name, const InterfaceSettings *requested, char *detail);

/*
 * Read the settings the kernel holds for the link. Fields the device does not
 * report (a vcan has no bit timing) are left out of fields. Returns false when
 * the interface is unknown.
 */
bool CanNetlink_Query(const char *interface_name, InterfaceSettings *effective);

/*
 * The configured nominal bitrate (bits/s), so the agent can advertise it for
 * rate-matched pacing. Returns 0 when the interface is unknown, is not a CAN
 * device, or has no bit timing set.
 */
uint32_t CanNetlink_GetBitrate(const char *interface_name);
bool CanNetlink_InterfaceExists(const char *interface_name);

/*
 * Create a vcan, already up and with the CAN FD MTU so it carries FD frames
 * too (needs CAP_NET_ADMIN). Fails if the name is taken.
 */
bool CanNetlink_CreateVcan(const char *interface_name);
