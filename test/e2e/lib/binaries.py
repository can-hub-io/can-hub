"""Locate the can-hub binaries under test."""

from __future__ import annotations

import os
from pathlib import Path

BIN_DIR = Path(os.environ.get("CAN_HUB_BIN_DIR", "/work/build/x86_64/release"))


def binary(name: str) -> str:
    path = BIN_DIR / name
    if not path.exists():
        raise FileNotFoundError(f"missing binary {path} (set CAN_HUB_BIN_DIR)")
    return str(path)
