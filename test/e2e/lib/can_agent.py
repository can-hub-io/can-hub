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

    def peer_id_on(self, hub) -> int:
        return self._single_peer_on(hub).peer_id

    def source_port_on(self, hub) -> int:
        return int(self._single_peer_on(hub).origin.rsplit(":", 1)[1])

    def wait_replaced_port(self, hub, old_port: int, timeout: float) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            mine = self._peers_on(hub)
            if len(mine) == 1 and int(mine[0].origin.rsplit(":", 1)[1]) != old_port:
                return
            time.sleep(0.1)
        raise TimeoutError(
            f"agent {self.config.name} still held by port {old_port}: {self._peers_on(hub)}\n{hub.process.read_log()}"
        )

    def wait_gone_from(self, hub, timeout: float) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if not self._peers_on(hub):
                return
            time.sleep(0.5)
        raise TimeoutError(f"hub still holds agent {self.config.name}: {self._peers_on(hub)}")

    def _peers_on(self, hub):
        return [peer for peer in hub.peers() if peer.role == "agent" and peer.name == self.config.name]

    def _single_peer_on(self, hub):
        mine = self._peers_on(hub)
        if len(mine) != 1:
            raise AssertionError(f"agent {self.config.name} expected once among hub peers, found {len(mine)}")
        return mine[0]

    def stop(self) -> None:
        self.process.stop()
