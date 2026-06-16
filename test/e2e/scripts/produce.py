"""Write a tight burst of frames into a socketcand channel.

Run inside the producer Server's namespace. Opens one python-can socketcand Bus
on the bridge's local socketcand server and sends <count> frames back to back
(no inter-frame gap), the firmware-upgrade write pattern. Prints how many sends
the backend accepted as a JSON summary.

Usage: produce.py <host> <port> <channel> <count>
"""

import json
import sys
import time

import can


def main() -> None:
    host, port, channel, count = sys.argv[1], int(sys.argv[2]), sys.argv[3], int(sys.argv[4])
    hold_seconds = float(sys.argv[5]) if len(sys.argv) > 5 else 0.0
    bus = can.Bus(interface="socketcand", host=host, port=port, channel=channel)
    sent = 0
    try:
        for index in range(count):
            message = can.Message(
                arbitration_id=0x123,
                data=index.to_bytes(8, "big"),
                is_extended_id=False,
            )
            bus.send(message)
            sent += 1
        time.sleep(hold_seconds)
    finally:
        bus.shutdown()
    print(json.dumps({"sent": sent}))


if __name__ == "__main__":
    main()
