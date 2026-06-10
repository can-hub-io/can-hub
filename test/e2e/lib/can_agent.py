"""CanAgent: a running can-hub-agent exporting a host's SocketCAN buses."""

from __future__ import annotations

import time
from pathlib import Path

from .binaries import binary
from .configuration import AgentConfig
from .server import Server


class CanAgent:
    def __init__(self, server: Server, config: AgentConfig, process):
        self.server = server
        self.config = config
        self.process = process

    @classmethod
    def start(cls, server: Server, config: AgentConfig) -> "CanAgent":
        if not config.state_dir:
            config.state_dir = str(server.work_dir / "agent-state")
        Path(config.state_dir).mkdir(parents=True, exist_ok=True)
        process = server.exec(binary("can-hub-agent"), *config.to_args(),
                              background=True, log_name="can-hub-agent")
        return cls(server, config, process)

    @staticmethod
    def show_identity(server: Server, state_dir: str) -> str:
        result = server.exec(binary("can-hub-agent"), "--show-identity",
                             "--state-dir", state_dir)
        return result.stdout.strip()

    def wait_registered(self, hub, timeout: float = 5.0) -> None:
        deadline = time.monotonic() + timeout
        want = len(self.config.interfaces)
        while time.monotonic() < deadline:
            if not self.process.is_alive():
                raise RuntimeError(f"agent exited:\n{self.process.read_log()}")
            mine = [row for row in hub.interfaces() if row.agent == self.config.name]
            if len(mine) >= want:
                return
            time.sleep(0.1)
        raise TimeoutError(
            f"agent {self.config.name} did not register {want} interfaces\n{self.process.read_log()}"
        )

    def stop(self) -> None:
        self.process.stop()
