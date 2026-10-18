#include "platform/linux/quic/quic_egress.h"

#include "platform/linux/clock/clock.h"

#define PACKET_BUFFER_SIZE 1452
/* Must equal the connection's fixed max_tx_udp_payload (size shaping disabled):
 * ngtcp2 then fills stream packets to exactly this size while data remains, so
 * consecutive packets are equal-sized and coalesce into one GSO send. A mismatch
 * would make every packet "short" and defeat coalescing. */
#define GSO_SEGMENT_SIZE 1452
#define EGRESS_BATCH_SEGMENTS 8
#define EGRESS_BATCH_SIZE (GSO_SEGMENT_SIZE * EGRESS_BATCH_SEGMENTS)

static void flushBatch(const QuicEgressSink *sink, const uint8_t *batch, size_t length);
static bool finishOpenPacket(QuicConnection *connection, const QuicEgressSink *sink, const QuicPacketBuffer *packet);
static ngtcp2_ssize writeNextPacket(
    QuicConnection *connection,
    QuicControlChannel *control,
    bool control_writable,
    uint8_t *packet,
    size_t *consumed
);

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

bool QuicEgress_FlushDatagrams(QuicConnection *connection, const QuicEgressSink *sink, QuicDatagramBacklog *backlog)
{
    uint8_t packet_data[PACKET_BUFFER_SIZE];
    QuicPacketBuffer packet = { packet_data, sizeof(packet_data), Clock_MonotonicNs() };
    const uint8_t *front;
    size_t front_size;
    ngtcp2_ssize bytes_written;
    bool accepted;
    bool packet_open = false;

    while (!QuicDatagramBacklog_IsEmpty(backlog)) {
        front = QuicDatagramBacklog_Front(backlog, &front_size);
        bytes_written = QuicConnection_PackDatagram(connection, &packet, front, front_size, &accepted);
        if (bytes_written == NGTCP2_ERR_WRITE_MORE) {
            QuicDatagramBacklog_PopFront(backlog);
            packet_open = true;
            continue;
        }
        if (bytes_written < 0) {
            return false;
        }

        packet_open = false;
        if (bytes_written > 0) {
            sink->send(sink->context, packet_data, (size_t)bytes_written, (size_t)bytes_written);
        }
        if (accepted) {
            QuicDatagramBacklog_PopFront(backlog);
        } else if (bytes_written == 0) {
            break;
        }
    }

    if (!packet_open) {
        return true;
    }

    return finishOpenPacket(connection, sink, &packet);
}

bool QuicEgress_Drain(QuicConnection *connection, QuicControlChannel *control, const QuicEgressSink *sink)
{
    uint8_t batch[EGRESS_BATCH_SIZE];
    size_t batch_length = 0;
    size_t consumed;
    bool control_writable = control->stream_id != QUIC_CONTROL_NO_STREAM;
    ngtcp2_ssize bytes_written;

    for (;;) {
        bytes_written = writeNextPacket(connection, control, control_writable, batch + batch_length, &consumed);
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

static ngtcp2_ssize writeNextPacket(
    QuicConnection *connection,
    QuicControlChannel *control,
    bool control_writable,
    uint8_t *packet,
    size_t *consumed
)
{
    const uint8_t *pending_data = NULL;
    size_t pending_size = 0;
    ngtcp2_ssize bytes_written;
    bool finished;

    *consumed = 0;
    if (control_writable) {
        pending_size = QuicControlChannel_PendingTx(control, &pending_data);
    }
    if (control_writable && pending_size > 0) {
        return QuicConnection_WriteStream(
            connection,
            packet,
            GSO_SEGMENT_SIZE,
            control->stream_id,
            pending_data,
            pending_size,
            consumed
        );
    }
    if (!control_writable || !QuicControlChannel_FinishDue(control)) {
        return QuicConnection_WritePacket(connection, packet, GSO_SEGMENT_SIZE);
    }

    bytes_written = QuicConnection_WriteStreamFinish(
        connection,
        packet,
        GSO_SEGMENT_SIZE,
        control->stream_id,
        &finished
    );
    if (finished) {
        QuicControlChannel_MarkFinishSent(control);
    }

    return bytes_written;
}

static void flushBatch(const QuicEgressSink *sink, const uint8_t *batch, size_t length)
{
    if (length == 0) {
        return;
    }

    sink->send(sink->context, batch, length, GSO_SEGMENT_SIZE);
}

static bool finishOpenPacket(QuicConnection *connection, const QuicEgressSink *sink, const QuicPacketBuffer *packet)
{
    ngtcp2_ssize bytes_written = QuicConnection_FinishPacket(connection, packet);

    if (bytes_written < 0) {
        return false;
    }
    if (bytes_written > 0) {
        sink->send(sink->context, packet->data, (size_t)bytes_written, (size_t)bytes_written);
    }

    return true;
}
