#ifndef PLATFORM_LINUX_QUIC_QUIC_EGRESS_H
#define PLATFORM_LINUX_QUIC_QUIC_EGRESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "platform/linux/quic/quic_connection.h"
#include "platform/linux/quic/quic_control_channel.h"

/*
 * Shared egress pump for QUIC transports: writes pending control stream
 * data and protocol packets into UDP-sized packets and hands them to the
 * sink. On false the connection is broken; the caller tears it down.
 */
typedef struct QuicEgressSink {
    void *context;
    void (*send)(void *context, const uint8_t *packet, size_t size);
} QuicEgressSink;

bool QuicEgress_FlushDatagram(
    QuicConnection *connection,
    const QuicEgressSink *sink,
    const uint8_t *datagram,
    size_t datagram_size,
    bool *accepted
);
bool QuicEgress_Drain(QuicConnection *connection, QuicControlChannel *control, const QuicEgressSink *sink);

#endif
