#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * rtnetlink helpers to reconfigure a CAN interface from the agent: bring the
 * link up/down and set the nominal bitrate (the kernel derives the bit timing
 * from the controller clock). Both need CAP_NET_ADMIN. A bitrate change
 * requires the link down first — the caller sequences down/set/up.
 */
bool CanNetlink_SetLink(const char *interface_name, bool up);
bool CanNetlink_SetBitrate(const char *interface_name, uint32_t bitrate);
