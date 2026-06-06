#include "platform/linux/quic/quic_egress.h"

#define PACKET_BUFFER_SIZE 1452

bool QuicEgress_FlushDatagram(
    QuicConnection *connection,
    const QuicEgressSink *sink,
    const uint8_t *datagram,
    size_t datagram_size,
    bool *accepted
)
{
    uint8_t packet[PACKET_BUFFER_SIZE];
    ngtcp2_ssize bytes_written;

    bytes_written = QuicConnection_WriteDatagram(
        connection,
        packet,
        sizeof(packet),
        datagram,
        datagram_size,
        accepted
    );
    if (bytes_written < 0) {
        return false;
    }
    if (bytes_written > 0) {
        sink->send(sink->context, packet, (size_t)bytes_written);
    }

    return true;
}

bool QuicEgress_Drain(QuicConnection *connection, QuicControlChannel *control, const QuicEgressSink *sink)
{
    uint8_t packet[PACKET_BUFFER_SIZE];
    const uint8_t *pending_data = NULL;
    size_t pending_size = 0;
    size_t consumed;
    bool control_writable = control->stream_id != QUIC_CONTROL_NO_STREAM;
    ngtcp2_ssize bytes_written;

    for (;;) {
        if (control_writable) {
            pending_size = QuicControlChannel_PendingTx(control, &pending_data);
        }

        if (control_writable && pending_size > 0) {
            bytes_written = QuicConnection_WriteStream(
                connection,
                packet,
                sizeof(packet),
                control->stream_id,
                pending_data,
                pending_size,
                &consumed
            );
        } else {
            bytes_written = QuicConnection_WritePacket(connection, packet, sizeof(packet));
            consumed = 0;
        }

        if (bytes_written == NGTCP2_ERR_STREAM_DATA_BLOCKED) {
            control_writable = false;
            continue;
        }
        if (bytes_written < 0) {
            return false;
        }
        if (bytes_written == 0) {
            return true;
        }

        sink->send(sink->context, packet, (size_t)bytes_written);
        QuicControlChannel_MarkSent(control, consumed);
    }
}
