"""Server: the abstraction of a host. Backed by a Linux network namespace.

A Server knows nothing about can-hub — only how to run commands, own vcan
interfaces and capture traffic inside its namespace. The Can* wrappers layer
the product on top.
"""

from __future__ import annotations

from pathlib import Path

from .process import Process
from .shell import Result, netns_argv, run


class Server:
    def __init__(self, name: str, ip: str, latency_ms: float, bench, work_dir: Path):
        self.name = name
        self.fqdn = name
        self.ip = ip
        self.latency_ms = latency_ms
        self._bench = bench
        self.work_dir = work_dir
        self.log_dir = work_dir / "logs"
        self.log_dir.mkdir(parents=True, exist_ok=True)
        self._log_seq = 0
        self._processes: list[Process] = []

    @property
    def netns(self) -> str:
        return self.name

    def exec(self, *argv: str, background: bool = False, log_name: str | None = None,
             check: bool = True, timeout: float | None = 30.0):
        full = netns_argv(self.netns, list(argv))
        if not background:
            return run(full, check=check, timeout=timeout)
        process = Process(self, full, self._next_log(log_name or argv_basename(argv)))
        self._processes.append(process)
        return process

    def make_vcan(self, name: str = "vcan0") -> str:
        self.exec("ip", "link", "add", "dev", name, "type", "vcan", check=False)
        self.exec("ip", "link", "set", name, "up")
        return name

    def cangen(self, interface: str, gap_ms: float, count: int,
               can_id: str | None = None, length: int = 8) -> Process:
        argv = ["cangen", interface, "-g", str(gap_ms), "-n", str(count), "-L", str(length)]
        if can_id is not None:
            argv += ["-I", can_id]
        return self.exec(*argv, background=True, log_name="cangen")

    def candump(self, *interfaces: str, log_name: str = "candump") -> Process:
        return self.exec("candump", "-L", *interfaces, background=True, log_name=log_name)

    def reachable(self, other: "Server") -> bool:
        return self._bench.reachable(self, other)

    def stop_all(self) -> None:
        for process in self._processes:
            process.stop()
        self._processes.clear()

    def _next_log(self, label: str) -> Path:
        self._log_seq += 1
        return self.log_dir / f"{self._log_seq:02d}-{label}.log"


def argv_basename(argv) -> str:
    return Path(argv[0]).name if argv else "proc"
