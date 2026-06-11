"""Bench: builds the whole test bed inside one privileged container.

One Linux network namespace per Server, joined to a bridge, with tc/netem
modelling per-host latency. Reachability follows a NAT-style matrix (WAN sits
behind NAT: it dials out but is not reachable inbound from LOCAL/LAN).
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

from .server import Server
from .shell import run

BRIDGE = "benchbr"
SUBNET = "10.0.0"
HOSTS_MARKER = "# can-hub-bench"


@dataclass
class ServerSpec:
    name: str
    host: int            # last octet
    latency_ms: float
    zone: str            # "local" | "lan" | "wan"


# WAN is behind NAT: reachable() encodes who can open a connection to whom.
DEFAULT_SPECS = [
    ServerSpec("local", 2, 0.0, "local"),
    ServerSpec("lan1", 3, 1.5, "lan"),
    ServerSpec("wan1", 4, 50.0, "wan"),
]


class Bench:
    def __init__(self, specs: list[ServerSpec] | None = None,
                 work_dir: str = "/tmp/can-hub-bench"):
        self.specs = specs or DEFAULT_SPECS
        self.work_dir = Path(work_dir)
        self.servers: dict[str, Server] = {
            spec.name: Server(
                spec.name, f"{SUBNET}.{spec.host}", spec.latency_ms,
                self, self.work_dir / spec.name,
            )
            for spec in self.specs
        }
        self._ready = False

    def setup(self) -> None:
        if self._ready:
            return
        self.work_dir.mkdir(parents=True, exist_ok=True)
        run(["modprobe", "vcan"], check=False)
        self._reset()
        run(["ip", "link", "add", BRIDGE, "type", "bridge"])
        run(["ip", "link", "set", BRIDGE, "up"])
        hosts_lines = []
        for spec in self.specs:
            self._make_namespace(spec)
            hosts_lines.append(f"{SUBNET}.{spec.host} {spec.name}")
        self._write_hosts(hosts_lines)
        self._ready = True

    def teardown(self) -> None:
        for server in self.servers.values():
            server.stop_all()
        self._reset()
        self._ready = False

    def reachable(self, src: Server, dst: Server) -> bool:
        src_zone = self._zone(src.name)
        dst_zone = self._zone(dst.name)
        if dst_zone == "wan":
            # WAN is behind NAT: only WAN itself initiates outbound.
            return src_zone == "wan"
        return True

    # ---------- private ----------

    def _zone(self, name: str) -> str:
        return next(s.zone for s in self.specs if s.name == name)

    def _make_namespace(self, spec: ServerSpec) -> None:
        ns = spec.name
        host_veth = f"{ns}h"
        ns_veth = f"{ns}p"
        run(["ip", "netns", "add", ns])
        run(["ip", "link", "add", host_veth, "type", "veth", "peer", "name", ns_veth])
        run(["ip", "link", "set", host_veth, "master", BRIDGE])
        run(["ip", "link", "set", host_veth, "up"])
        run(["ip", "link", "set", ns_veth, "netns", ns])
        self._ns(ns, "ip", "link", "set", "lo", "up")
        self._ns(ns, "ip", "link", "set", ns_veth, "name", "eth0")
        self._ns(ns, "ip", "addr", "add", f"{SUBNET}.{spec.host}/24", "dev", "eth0")
        self._ns(ns, "ip", "link", "set", "eth0", "up")
        if spec.latency_ms > 0:
            self._ns(ns, "tc", "qdisc", "add", "dev", "eth0", "root",
                     "netem", "delay", f"{spec.latency_ms}ms")

    def _ns(self, ns: str, *argv: str) -> None:
        run(["ip", "netns", "exec", ns, *argv])

    def _reset(self) -> None:
        for spec in self.specs:
            run(["bash", "-c",
                 f"ip netns pids {spec.name} 2>/dev/null | xargs -r kill -9"], check=False)
            run(["ip", "netns", "del", spec.name], check=False)
            run(["ip", "link", "del", f"{spec.name}h"], check=False)
        run(["ip", "link", "del", BRIDGE], check=False)
        self._write_hosts([])

    def _write_hosts(self, lines: list[str]) -> None:
        path = Path("/etc/hosts")
        kept = [
            line for line in path.read_text().splitlines()
            if HOSTS_MARKER not in line
        ]
        block = [f"{line}  {HOSTS_MARKER}" for line in lines]
        path.write_text("\n".join(kept + block) + "\n")
