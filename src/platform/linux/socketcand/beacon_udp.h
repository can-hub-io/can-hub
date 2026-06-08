#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * UDP broadcast helper for the socketcand discovery beacon. Open one socket
 * with SO_BROADCAST, then send each rendered CANBeacon document to the global
 * broadcast address on the discovery port. Best-effort.
 */
int32_t BeaconUdp_Open(void);
void BeaconUdp_Send(int32_t fd, uint16_t port, const uint8_t *data, size_t size);
void BeaconUdp_Close(int32_t fd);
