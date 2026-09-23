"""Write through one socketcand connection and count what comes back on it.

Run inside the bridge Server's namespace. Opens one python-can socketcand Bus,
sends <count> frames with <own_id>, then keeps receiving on the same connection
until <seconds> have passed since the open. What comes back is split into its
own writes echoed back and traffic that reached the bus from elsewhere, so a
silent connection cannot pass for a suppressed echo. Prints a JSON summary.

Usage: echo_probe.py <host> <port> <channel> <count> <own_id> <seconds>
"""

import json
import sys
import time

import can

RECEIVE_TIMEOUT_SECONDS = 0.2
PAYLOAD_SIZE = 8


def write_burst(bus: can.BusABC, count: int, own_id: int) -> int:
    for index in range(count):
        bus.send(can.Message(arbitration_id=own_id, data=index.to_bytes(PAYLOAD_SIZE, "big"),
                             is_extended_id=False))
    return count


def receive_until(bus: can.BusABC, deadline: float, own_id: int) -> dict[str, int]:
    received = {"own": 0, "other": 0}
    while time.monotonic() < deadline:
        message = bus.recv(timeout=RECEIVE_TIMEOUT_SECONDS)
        if message is None:
            continue
        received["own" if message.arbitration_id == own_id else "other"] += 1
    return received


def main() -> None:
    host, port, channel = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    count, own_id, seconds = int(sys.argv[4]), int(sys.argv[5], 16), float(sys.argv[6])
    bus = can.Bus(interface="socketcand", host=host, port=port, channel=channel)
    deadline = time.monotonic() + seconds
    try:
        sent = write_burst(bus, count, own_id)
        received = receive_until(bus, deadline, own_id)
    finally:
        bus.shutdown()
    print(json.dumps({"sent": sent, **received}))


if __name__ == "__main__":
    main()
