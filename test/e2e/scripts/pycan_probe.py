"""Drive the canhub python-can backend on the bench.

usage: pycan_probe.py <url> <channel> dump
       pycan_probe.py <url> <channel> send <can-id-hex> <payload-hex>
"""

import sys

import can


def main():
    url, channel, action = sys.argv[1], sys.argv[2], sys.argv[3]
    bus = can.Bus(interface="canhub", channel=channel, url=url)

    if action == "send":
        message = can.Message(
            arbitration_id=int(sys.argv[4], 16),
            data=bytes.fromhex(sys.argv[5]),
            is_extended_id=False,
        )
        bus.send(message)
        bus.shutdown()
        return

    print(f"dumping {channel}", flush=True)
    while True:
        message = bus.recv(timeout=1.0)
        if message is not None:
            print(f"{message.arbitration_id:03X}#{message.data.hex().upper()}", flush=True)


if __name__ == "__main__":
    main()
