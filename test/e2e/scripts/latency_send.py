"""Stream CAN frames stamped with the wall-clock send time in the payload.

Each frame's 8-byte payload is the send time in microseconds (big-endian), so a
consumer can compute the frame's age = receive_time - send_time. The bench runs
every Server in one host, so both clocks are the same. Used to measure
end-to-end latency (and standing-queue bufferbloat) under load.

Usage: latency_send.py <iface> <can-id-hex> <gap-seconds> <duration-seconds>
"""

import socket
import struct
import sys
import time

CAN_FRAME_FORMAT = "=IB3x8s"


def main() -> None:
    iface, can_id = sys.argv[1], int(sys.argv[2], 16)
    gap, duration = float(sys.argv[3]), float(sys.argv[4])

    bus = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    bus.bind((iface,))

    deadline = time.monotonic() + duration
    sent = 0
    while time.monotonic() < deadline:
        payload = struct.pack(">Q", int(time.time() * 1_000_000))
        bus.send(struct.pack(CAN_FRAME_FORMAT, can_id, 8, payload))
        sent += 1
        if gap > 0:
            time.sleep(gap)

    print(sent)


if __name__ == "__main__":
    main()
