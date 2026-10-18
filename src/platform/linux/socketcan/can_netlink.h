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

/*
 * Read the configured nominal bitrate (bits/s) the kernel derived for the link,
 * so the agent can advertise it for rate-matched pacing. Returns 0 when the
 * interface is unknown, is not a CAN device, or has no bit timing set.
 */
uint32_t CanNetlink_GetBitrate(const char *interface_name);
bool CanNetlink_InterfaceExists(const char *interface_name);

/*
 * Create a vcan, already up and with the CAN FD MTU so it carries FD frames
 * too (needs CAP_NET_ADMIN). Fails if the name is taken.
 */
bool CanNetlink_CreateVcan(const char *interface_name);
