"""Stream CAN frames stamped with a sequence number and a send time, for the
protocol benchmark. The 8-byte payload is big-endian: a u32 sequence number
followed by the low 32 bits of the send time in microseconds (wraps every
~71 min, fine for a short run). One stream lets the consumer measure loss,
reorder, frame rate, latency and jitter at once.

Usage: bench_send.py <iface> <can-id-hex> <count> <gap-seconds>
"""

import socket
import struct
import sys
import time

CAN_FRAME_FORMAT = "=IB3x8s"


def main() -> None:
    iface, can_id = sys.argv[1], int(sys.argv[2], 16)
    count, gap = int(sys.argv[3]), float(sys.argv[4])

    bus = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    bus.bind((iface,))

    for seq in range(count):
        send_us = int(time.time() * 1_000_000) & 0xFFFFFFFF
        payload = struct.pack(">II", seq, send_us)
        bus.send(struct.pack(CAN_FRAME_FORMAT, can_id, 8, payload))
        if gap > 0:
            time.sleep(gap)

    print(count)


if __name__ == "__main__":
    main()
