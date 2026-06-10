"""Low-level shell helpers shared by the bench library.

Everything runs inside the single privileged container; per-Server isolation
comes from Linux network namespaces, so commands are wrapped with
``ip netns exec <ns>`` rather than dispatched to separate hosts.
"""

from __future__ import annotations

import shlex
import subprocess
from dataclasses import dataclass


@dataclass
class Result:
    argv: list[str]
    returncode: int
    stdout: str
    stderr: str

    @property
    def ok(self) -> bool:
        return self.returncode == 0

    def check(self) -> "Result":
        if not self.ok:
            raise RuntimeError(
                f"command failed ({self.returncode}): {shlex.join(self.argv)}\n{self.stderr}"
            )
        return self


def run(argv: list[str], check: bool = True, timeout: float | None = 30.0) -> Result:
    completed = subprocess.run(
        argv,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=timeout,
    )
    result = Result(argv, completed.returncode, completed.stdout, completed.stderr)
    if check:
        result.check()
    return result


def netns_argv(netns: str, argv: list[str]) -> list[str]:
    return ["ip", "netns", "exec", netns, *argv]
