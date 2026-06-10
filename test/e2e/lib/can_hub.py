"""CanHub: a running can-hub plus admin access through its local cli."""

from __future__ import annotations

import time
from pathlib import Path

from .binaries import binary
from .configuration import HubConfig
from .rows import (
    HubStatus, parse_clients, parse_interfaces, parse_peers, parse_status,
)
from .server import Server
from .shell import Result


class CanHub:
    def __init__(self, server: Server, config: HubConfig, process, unix_socket: str):
        self.server = server
        self.config = config
        self.process = process
        self.unix_socket = unix_socket

    @classmethod
    def start(cls, server: Server, config: HubConfig | None = None) -> "CanHub":
        config = config or HubConfig()
        if not config.unix_socket:
            config.unix_socket = str(server.work_dir / "hub.sock")
        if not config.state_dir:
            config.state_dir = str(server.work_dir / "hub-state")
        Path(config.unix_socket).parent.mkdir(parents=True, exist_ok=True)
        Path(config.state_dir).mkdir(parents=True, exist_ok=True)
        process = server.exec(binary("can-hub"), *config.to_args(),
                              background=True, log_name="can-hub")
        hub = cls(server, config, process, config.unix_socket)
        hub._wait_listening()
        return hub

    def cli(self, *args: str, check: bool = True) -> Result:
        return self.server.exec(
            binary("can-hub-cli"), "--connect", f"unix://{self.unix_socket}", *args,
            check=check,
        )

    def status(self) -> HubStatus:
        return parse_status(self.cli("status").stdout)

    def peers(self):
        return parse_peers(self.cli("peers").stdout)

    def clients(self):
        return parse_clients(self.cli("clients").stdout)

    def interfaces(self):
        return parse_interfaces(self.cli("interfaces").stdout)

    def stop(self) -> None:
        self.process.stop()

    def _wait_listening(self, timeout: float = 5.0) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if not self.process.is_alive():
                raise RuntimeError(f"hub exited:\n{self.process.read_log()}")
            if Path(self.unix_socket).exists() and self.cli("status", check=False).ok:
                return
            time.sleep(0.05)
        raise TimeoutError(f"hub not listening on {self.unix_socket}\n{self.process.read_log()}")
