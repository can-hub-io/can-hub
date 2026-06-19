#ifndef PLATFORM_LINUX_QUIC_QUIC_UDP_GSO_H
#define PLATFORM_LINUX_QUIC_QUIC_UDP_GSO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <sys/socket.h>

/*
 * Send a coalesced batch of equal-sized UDP datagrams in one syscall via UDP
 * GSO (UDP_SEGMENT): the kernel splits the buffer into segment_size chunks, the
 * last possibly shorter. Cuts the per-packet syscall/crypto-independent cost
 * that makes a userspace QUIC hub fall behind kernel TCP under load. Falls back
 * to one send per segment when the kernel lacks GSO; gso_unsupported caches that
 * so the failed probe runs only once. A NULL address sends on a connected
 * socket. size <= segment_size sends a single datagram.
 */
void QuicUdpGso_Send(
    int32_t fd,
    const struct sockaddr *address,
    socklen_t address_length,
    const uint8_t *data,
    size_t size,
    size_t segment_size,
    bool *gso_unsupported
);

#endif
