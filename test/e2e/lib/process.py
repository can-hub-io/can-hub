"""Handle to a long-running binary launched inside a Server's namespace."""

from __future__ import annotations

import os
import signal
import subprocess
import time
from pathlib import Path


class Process:
    def __init__(self, server, argv: list[str], logfile: Path):
        self.server = server
        self.argv = argv
        self.logfile = logfile
        self._log = open(logfile, "w+")
        self._popen = subprocess.Popen(
            argv,
            stdout=self._log,
            stderr=subprocess.STDOUT,
        )

    @property
    def pid(self) -> int:
        return self._popen.pid

    def is_alive(self) -> bool:
        return self._popen.poll() is None

    def read_log(self) -> str:
        return Path(self.logfile).read_text(errors="replace")

    def wait_ready(self, pattern, timeout: float = 5.0) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if not self.is_alive():
                raise RuntimeError(
                    f"process exited before ready: {' '.join(self.argv)}\n{self.read_log()}"
                )
            text = self.read_log()
            if callable(pattern):
                if pattern():
                    return
            elif pattern in text:
                return
            time.sleep(0.05)
        raise TimeoutError(
            f"not ready in {timeout}s (waited for {pattern!r}): {' '.join(self.argv)}\n{self.read_log()}"
        )

    def cpu_jiffies(self) -> int:
        fields = Path(f"/proc/{self.pid}/stat").read_text().rsplit(")", 1)[1].split()
        return int(fields[11]) + int(fields[12])

    def rss_kb(self) -> int:
        for line in Path(f"/proc/{self.pid}/status").read_text().splitlines():
            if line.startswith("VmRSS:"):
                return int(line.split()[1])
        return 0

    def stop(self, sig: int = signal.SIGTERM, timeout: float = 3.0) -> int:
        if self._popen.poll() is None:
            self._popen.send_signal(sig)
            try:
                self._popen.wait(timeout)
            except subprocess.TimeoutExpired:
                self._popen.kill()
                self._popen.wait(timeout)
        self._log.close()
        return self._popen.returncode

    def wait(self, timeout: float | None = None) -> int:
        return self._popen.wait(timeout)
