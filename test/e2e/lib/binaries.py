"""Locate the can-hub binaries under test."""

from __future__ import annotations

import os
from pathlib import Path

BIN_DIR = Path(os.environ.get("CAN_HUB_BIN_DIR", "/work/build/x86_64/release"))

# The web daemon is a separate Rust crate, built into its own target dir rather
# than the C release tree.
WEB_BIN = Path(os.environ.get("CAN_HUB_WEB_BIN", "/work/web/daemon/target/release/can-hub-web"))


def binary(name: str) -> str:
    path = BIN_DIR / name
    if not path.exists():
        raise FileNotFoundError(f"missing binary {path} (set CAN_HUB_BIN_DIR)")
    return str(path)


def web_binary() -> str:
    if not WEB_BIN.exists():
        raise FileNotFoundError(
            f"missing can-hub-web {WEB_BIN} (build it: cd web/daemon && cargo build --release, "
            f"or set CAN_HUB_WEB_BIN)"
        )
    return str(WEB_BIN)
