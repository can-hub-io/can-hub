"""Server: the abstraction of a host. Backed by a Linux network namespace.

A Server knows nothing about can-hub — only how to run commands, own vcan
interfaces and capture traffic inside its namespace. The Can* wrappers layer
the product on top.
"""

from __future__ import annotations

import json
from pathlib import Path

from .process import Process
from .shell import Result, netns_argv, run

CANDUMP_RECEIVE_BUFFER_BYTES = 4 * 1024 * 1024
CANDUMP_ALL_ERRORS = "#FFFFFFFF"
CANFD_MTU = 72
CAN_MTU = 16
NAMESPACE_CONFIG_DIR = Path("/etc/netns")
NAMESERVER_PORT = 53
SILENT_RESOLVER_TIMEOUT_S = 15


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

    def link(self, name: str) -> dict:
        return json.loads(self.exec("ip", "-j", "link", "show", "dev", name).stdout)[0]

    def make_can_classic(self, name: str) -> None:
        self.exec("ip", "link", "set", name, "down")
        self.exec("ip", "link", "set", name, "mtu", str(CAN_MTU))
        self.exec("ip", "link", "set", name, "up")

    def enable_can_fd(self, name: str) -> None:
        self.exec("ip", "link", "set", name, "down")
        self.exec("ip", "link", "set", name, "mtu", str(CANFD_MTU))
        self.exec("ip", "link", "set", name, "up")

    def cangen(self, interface: str, gap_ms: float, count: int,
               can_id: str | None = None, length: int = 8) -> Process:
        argv = ["cangen", interface, "-g", str(gap_ms), "-n", str(count), "-L", str(length)]
        if can_id is not None:
            argv += ["-I", can_id]
        return self.exec(*argv, background=True, log_name="cangen")

    def candump(self, *interfaces: str, log_name: str = "candump", error_frames: bool = False) -> Process:
        if error_frames:
            interfaces = tuple(f"{interface},{CANDUMP_ALL_ERRORS}" for interface in interfaces)
        return self.exec("candump", "-L", "-r", str(CANDUMP_RECEIVE_BUFFER_BYTES), *interfaces,
                         background=True, log_name=log_name)

    def silence_dns(self) -> None:
        self._resolv_conf().parent.mkdir(parents=True, exist_ok=True)
        self._resolv_conf().write_text(f"nameserver {self.ip}\n"
                                       f"options timeout:{SILENT_RESOLVER_TIMEOUT_S} attempts:1\n")
        self.exec(*self._nameserver_drop_rule("-A"))

    def restore_dns(self) -> None:
        self._resolv_conf().unlink(missing_ok=True)
        self.exec(*self._nameserver_drop_rule("-D"), check=False)

    def reachable(self, other: "Server") -> bool:
        return self._bench.reachable(self, other)

    def stop_all(self) -> None:
        for process in self._processes:
            process.stop()
        self._processes.clear()

    def _resolv_conf(self) -> Path:
        return NAMESPACE_CONFIG_DIR / self.netns / "resolv.conf"

    def _nameserver_drop_rule(self, action: str) -> list[str]:
        return ["iptables", action, "INPUT", "-p", "udp", "--dport", str(NAMESERVER_PORT), "-j", "DROP"]

    def _next_log(self, label: str) -> Path:
        self._log_seq += 1
        return self.log_dir / f"{self._log_seq:02d}-{label}.log"


def argv_basename(argv) -> str:
    return Path(argv[0]).name if argv else "proc"
