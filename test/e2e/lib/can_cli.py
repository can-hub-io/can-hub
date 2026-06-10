"""CanCli: one-shot hub administration over a hub's local unix socket."""

from __future__ import annotations

from .binaries import binary
from .server import Server
from .shell import Result


class CanCli:
    @staticmethod
    def run(server: Server, unix_socket: str, *args: str, check: bool = True) -> Result:
        return server.exec(
            binary("can-hub-cli"), "--connect", f"unix://{unix_socket}", *args,
            check=check,
        )
