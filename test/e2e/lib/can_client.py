"""CanClient: the reference consumer (list/dump/send/attach/socketcand)."""

from __future__ import annotations

from pathlib import Path

from .binaries import binary
from .configuration import ClientConfig
from .rows import parse_client_dump, parse_list
from .server import Server
from .shell import Result


class CanClient:
    def __init__(self, server: Server, config: ClientConfig, process):
        self.server = server
        self.config = config
        self.process = process

    @classmethod
    def start(cls, server: Server, config: ClientConfig) -> "CanClient":
        if not config.state_dir:
            config.state_dir = str(server.work_dir / "client-state")
        Path(config.state_dir).mkdir(parents=True, exist_ok=True)
        process = server.exec(binary("can-hub-client"), *config.to_args(),
                              background=True, log_name=f"can-hub-client-{config.command}")
        return cls(server, config, process)

    @staticmethod
    def list(server: Server, connect: str | None = None):
        argv = [binary("can-hub-client")]
        if connect:
            argv += ["--connect", connect]
        argv += ["list"]
        return parse_list(server.exec(*argv).stdout)

    @staticmethod
    def send(server: Server, interface: str, frame: str, connect: str | None = None) -> Result:
        argv = [binary("can-hub-client")]
        if connect:
            argv += ["--connect", connect]
        argv += ["send", interface, frame]
        return server.exec(*argv, check=False)

    def dumped_frames(self):
        return parse_client_dump(self.process.read_log())

    def stop(self) -> None:
        self.process.stop()
