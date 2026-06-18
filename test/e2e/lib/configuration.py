"""Parametrisable launch configs for each binary, built from Robot or Python.

Each config renders to the binary's argument vector with ``to_args()``; the
Can* wrappers prepend the binary path and the Server prepends the namespace.
"""

from __future__ import annotations

from dataclasses import dataclass, field


@dataclass
class HubConfig:
    listen: list[str] = field(default_factory=list)
    state_dir: str | None = None
    unix_socket: str | None = None
    require_known_agents: bool = False
    cert: str | None = None
    key: str | None = None
    extra: list[str] = field(default_factory=list)

    def to_args(self) -> list[str]:
        argv: list[str] = []
        if self.state_dir:
            argv += ["--state-dir", self.state_dir]
        if self.unix_socket:
            argv += ["--listen", f"unix://{self.unix_socket}"]
        for address in self.listen:
            argv += ["--listen", address]
        if self.require_known_agents:
            argv += ["--require-known-agents"]
        if self.cert:
            argv += ["--cert", self.cert]
        if self.key:
            argv += ["--key", self.key]
        return argv + list(self.extra)


@dataclass
class AgentConfig:
    connect: str
    name: str
    interfaces: list[str] = field(default_factory=list)
    state_dir: str | None = None
    require_known_agents: bool = False
    extra: list[str] = field(default_factory=list)

    def to_args(self) -> list[str]:
        argv = ["--connect", self.connect, "--name", self.name]
        if self.state_dir:
            argv += ["--state-dir", self.state_dir]
        if self.require_known_agents:
            argv += ["--require-known-agents"]
        return argv + list(self.extra) + list(self.interfaces)


@dataclass
class ClientConfig:
    command: str
    connect: str | None = None
    args: list[str] = field(default_factory=list)
    no_echo: bool = False
    reliable: bool = False
    state_dir: str | None = None
    extra: list[str] = field(default_factory=list)

    def to_args(self) -> list[str]:
        argv: list[str] = []
        if self.connect:
            argv += ["--connect", self.connect]
        if self.state_dir:
            argv += ["--state-dir", self.state_dir]
        if self.no_echo:
            argv += ["--no-echo"]
        if self.reliable:
            argv += ["--reliable"]
        argv += [self.command]
        return argv + list(self.extra) + list(self.args)
