"""Drain one or more socketcand channels through a local bridge, count frames.

Run inside the consumer Server's namespace. One python-can socketcand Bus per
channel (each opens its own channel on the bridge's single hub peer, which is
exactly the multiplexed-peer case #59 is about). Reads as fast as it can for a
fixed window; prints a JSON summary of per-channel counts.

Usage: consume.py <host> <port> <seconds> <channel> [<channel> ...]
"""

import json
import sys
import threading
import time

import can


def drain(host: str, port: int, channel: str, deadline: float, counts: dict):
    bus = can.Bus(interface="socketcand", host=host, port=port, channel=channel)
    received = 0
    try:
        while time.monotonic() < deadline:
            message = bus.recv(timeout=0.2)
            if message is not None:
                received += 1
    finally:
        bus.shutdown()
        counts[channel] = received


def main() -> None:
    host, port, seconds = sys.argv[1], int(sys.argv[2]), float(sys.argv[3])
    channels = sys.argv[4:]
    deadline = time.monotonic() + seconds
    counts: dict[str, int] = {}
    threads = [
        threading.Thread(target=drain, args=(host, port, channel, deadline, counts))
        for channel in channels
    ]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    print(json.dumps(counts))


if __name__ == "__main__":
    main()
