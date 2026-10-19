"""Open a python-can socketcand Bus on a bridge that may still be starting.

The bridge listens before it has connected to the hub and cached the
interface list; until then it refuses the open. Retry until the deadline
instead of sleeping a fixed time before the first attempt.
"""

import time

import can

OPEN_TIMEOUT_SECONDS = 15.0
RETRY_PERIOD_SECONDS = 0.1


def open_socketcand_bus(host: str, port: int, channel: str) -> can.BusABC:
    deadline = time.monotonic() + OPEN_TIMEOUT_SECONDS
    while True:
        try:
            return can.Bus(interface="socketcand", host=host, port=port, channel=channel)
        except (OSError, can.CanError):
            if time.monotonic() > deadline:
                raise
            time.sleep(RETRY_PERIOD_SECONDS)
