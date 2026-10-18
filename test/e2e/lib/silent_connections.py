"""SilentConnections: TCP connections that connect and never speak (half-opens)."""

from __future__ import annotations

import json
from pathlib import Path

from .server import Server

SCRIPT = Path(__file__).resolve().parent.parent / "scripts" / "silent_connections.py"
EXIT_GRACE_SECONDS = 5


class SilentConnections:
    def __init__(self, process, timeout: float):
        self.process = process
        self.timeout = timeout

    @classmethod
    def open(cls, server: Server, host: str, port: int, count: int, timeout: float) -> "SilentConnections":
        process = server.exec("python3", str(SCRIPT), host, str(port), str(count), str(timeout),
                              background=True, log_name="silent-connections")
        return cls(process, timeout)

    def closed_after(self) -> list[float | None]:
        self.process.wait(timeout=self.timeout + EXIT_GRACE_SECONDS)
        return json.loads(self.process.read_log().strip().splitlines()[-1])["closed_after"]

    def count_closed_between(self, low: float, high: float) -> int:
        return len([seconds for seconds in self.closed_after() if seconds is not None and low <= seconds <= high])
