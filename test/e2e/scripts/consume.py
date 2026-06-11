"""Drain one or more socketcand channels through a local bridge, count frames.

Run inside the consumer Server's namespace. One python-can socketcand Bus per
channel argument (each opens its own channel on the bridge's single hub peer,
which is exactly the multiplexed-peer case #59 is about). A channel may be
repeated to hold several connections to the same bus (the duplicate-binding
case #68); repeats are reported under `<channel>@<n>`. Connections open
sequentially (concurrent opens of one interface trip a separate bridge race),
then all drain as fast as they can for a fixed window; prints a JSON summary
of per-connection counts.

Usage: consume.py <host> <port> <seconds> <channel> [<channel> ...]
"""

import json
import sys
import threading
import time

import can


def drain(bus: can.BusABC, key: str, deadline: float, counts: dict):
    received = 0
    try:
        while time.monotonic() < deadline:
            message = bus.recv(timeout=0.2)
            if message is not None:
                received += 1
    finally:
        bus.shutdown()
        counts[key] = received


def connection_keys(channels: list[str]) -> list[str]:
    seen: dict[str, int] = {}
    keys = []
    for channel in channels:
        occurrence = seen.get(channel, 0)
        seen[channel] = occurrence + 1
        keys.append(channel if occurrence == 0 else f"{channel}@{occurrence}")
    return keys


def main() -> None:
    host, port, seconds = sys.argv[1], int(sys.argv[2]), float(sys.argv[3])
    channels = sys.argv[4:]
    deadline = time.monotonic() + seconds
    counts: dict[str, int] = {}
    buses = [
        can.Bus(interface="socketcand", host=host, port=port, channel=channel)
        for channel in channels
    ]
    threads = [
        threading.Thread(target=drain, args=(bus, key, deadline, counts))
        for bus, key in zip(buses, connection_keys(channels))
    ]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    print(json.dumps(counts))


if __name__ == "__main__":
    main()
