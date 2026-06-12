"""CanHubWeb: a running can-hub-web daemon plus a REST client against it.

The daemon is an ordinary admin client of the hub unix socket and an HTTP server
towards browsers. It binds the Server's namespace IP (not loopback) so the
Robot process in the root namespace can reach it over the bench bridge.
"""

from __future__ import annotations

import time
from pathlib import Path

from .binaries import web_binary
from .can_hub import CanHub
from .rest import DEFAULT_SPEC, RestClient
from .server import Server


class CanHubWeb:
    def __init__(self, server: Server, process, base_url: str, db_path: str, rest: RestClient):
        self.server = server
        self.process = process
        self.base_url = base_url
        self.db_path = db_path
        self.rest = rest

    @classmethod
    def start(
        cls,
        server: Server,
        hub: CanHub,
        port: int = 8080,
        spec_path: str = DEFAULT_SPEC,
    ) -> "CanHubWeb":
        db_path = str(server.work_dir / "web.db")
        Path(db_path).unlink(missing_ok=True)
        listen = f"{server.ip}:{port}"
        process = server.exec(
            web_binary(),
            "--connect", hub.unix_socket,
            "--listen", listen,
            "--db", db_path,
            background=True,
            log_name="can-hub-web",
        )
        base_url = f"http://{server.ip}:{port}"
        web = cls(server, process, base_url, db_path, RestClient(base_url, spec_path))
        web._wait_ready()
        return web

    def new_unauthenticated_client(self) -> RestClient:
        return RestClient(self.base_url)

    def stop(self) -> None:
        self.process.stop()

    def _wait_ready(self, timeout: float = 10.0) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if not self.process.is_alive():
                raise RuntimeError(f"can-hub-web exited:\n{self.process.read_log()}")
            try:
                if self.rest.healthz_ok():
                    return
            except Exception:
                pass
            time.sleep(0.1)
        raise TimeoutError(f"can-hub-web not ready on {self.base_url}\n{self.process.read_log()}")
