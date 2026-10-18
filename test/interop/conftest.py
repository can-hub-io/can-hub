"""Interop bench: the current build against the last release built on OpenSSL.

INTEROP_CURRENT_BIN and INTEROP_BASELINE_BIN name the directories holding each
build's can-hub and can-hub-client; `make interop` sets both.
"""

from __future__ import annotations

import os
import socket
import subprocess
import time
from pathlib import Path

import pytest

READY_TIMEOUT_SECONDS = 10
READY_LINE = "ready"


class Hub:
    def __init__(self, binary_dir: Path, work_dir: Path, port: int):
        self.port = port
        self.log_path = work_dir / "hub.log"
        argv = [
            str(binary_dir / "can-hub"),
            "--listen", f"quic://127.0.0.1:{port}",
            "--listen", f"tls://127.0.0.1:{port}",
            "--listen", f"unix://{work_dir / 'hub.sock'}",
            "--state-dir", str(work_dir / "hub-state"),
        ]
        self._log = open(self.log_path, "w+")
        self._process = subprocess.Popen(argv, stdout=self._log, stderr=subprocess.STDOUT)
        self._wait_ready()

    def log(self) -> str:
        return self.log_path.read_text()

    def stop(self) -> None:
        self._process.terminate()
        self._process.wait(timeout=READY_TIMEOUT_SECONDS)
        self._log.close()

    def _wait_ready(self) -> None:
        deadline = time.monotonic() + READY_TIMEOUT_SECONDS
        while time.monotonic() < deadline:
            if READY_LINE in self.log():
                return
            time.sleep(0.05)
        raise TimeoutError(f"hub not ready:\n{self.log()}")


@pytest.fixture(scope="session")
def current_bin() -> Path:
    return binary_dir_from("INTEROP_CURRENT_BIN")


@pytest.fixture(scope="session")
def baseline_bin() -> Path:
    return binary_dir_from("INTEROP_BASELINE_BIN")


@pytest.fixture
def start_hub(tmp_path):
    hubs = []

    def start(binary_dir: Path) -> Hub:
        hub = Hub(binary_dir, tmp_path, free_port())
        hubs.append(hub)
        return hub

    yield start
    for hub in hubs:
        hub.stop()


def binary_dir_from(variable: str) -> Path:
    value = os.environ.get(variable)
    if not value:
        pytest.skip(f"{variable} not set (run through make interop)")
    return Path(value)


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]
