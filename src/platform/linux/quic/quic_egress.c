#include "platform/linux/quic/quic_egress.h"

#define PACKET_BUFFER_SIZE 1452
/* Must equal the connection's fixed max_tx_udp_payload (size shaping disabled):
 * ngtcp2 then fills stream packets to exactly this size while data remains, so
 * consecutive packets are equal-sized and coalesce into one GSO send. A mismatch
 * would make every packet "short" and defeat coalescing. */
#define GSO_SEGMENT_SIZE 1452
#define EGRESS_BATCH_SEGMENTS 8
#define EGRESS_BATCH_SIZE (GSO_SEGMENT_SIZE * EGRESS_BATCH_SEGMENTS)

static void flushBatch(const QuicEgressSink *sink, const uint8_t *batch, size_t length);

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
        sink->send(sink->context, packet, (size_t)bytes_written, (size_t)bytes_written);
    }

    return true;
}

bool QuicEgress_Drain(QuicConnection *connection, QuicControlChannel *control, const QuicEgressSink *sink)
{
    uint8_t batch[EGRESS_BATCH_SIZE];
    const uint8_t *pending_data = NULL;
    size_t pending_size = 0;
    size_t batch_length = 0;
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
                batch + batch_length,
                GSO_SEGMENT_SIZE,
                control->stream_id,
                pending_data,
                pending_size,
                &consumed
            );
        } else {
            bytes_written = QuicConnection_WritePacket(connection, batch + batch_length, GSO_SEGMENT_SIZE);
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
            flushBatch(sink, batch, batch_length);
            return true;
        }

        QuicControlChannel_MarkSent(control, consumed);
        batch_length += (size_t)bytes_written;
        if ((size_t)bytes_written < GSO_SEGMENT_SIZE || batch_length + GSO_SEGMENT_SIZE > EGRESS_BATCH_SIZE) {
            flushBatch(sink, batch, batch_length);
            batch_length = 0;
        }
    }
}

/* ---------- private ---------- */

static void flushBatch(const QuicEgressSink *sink, const uint8_t *batch, size_t length)
{
    if (length == 0) {
        return;
    }

    sink->send(sink->context, batch, length, GSO_SEGMENT_SIZE);
}
