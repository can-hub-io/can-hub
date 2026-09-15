"""Locate a built libcanhub.so so importing canhub works in tests.

The native library is only loaded to satisfy the import; every test that
exercises behaviour monkeypatches ``canhub._native.lib``, so the real symbols
are never called. When no library is found the canhub tests are skipped.

Candidates are opened before being accepted: a tree that has cross-built for
another architecture leaves a libcanhub.so the host cannot load, and taking it
on name alone fails the whole collection rather than falling through.
"""

import ctypes
import glob
import os
import pathlib
import warnings

collect_ignore_glob = []


def _ensure_library():
    existing = os.environ.get("CANHUB_LIBRARY")
    if existing and os.path.exists(existing):
        return True

    root = pathlib.Path(__file__).resolve().parents[2]
    patterns = [
        root / "build" / "**" / "libcanhub.so",
        root / "python" / "canhub" / "libcanhub.so",
    ]
    for pattern in patterns:
        for path in sorted(glob.glob(str(pattern), recursive=True)):
            if not _loadable(path):
                continue
            os.environ["CANHUB_LIBRARY"] = path
            return True
    return False


def _loadable(path):
    try:
        ctypes.CDLL(path)
    except OSError:
        return False
    return True


if not _ensure_library():
    collect_ignore_glob = ["test_*.py"]
    warnings.warn("libcanhub.so not found; skipping canhub tests (build with make release)")
