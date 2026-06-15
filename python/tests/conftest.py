"""Locate a built libcanhub.so so importing canhub works in tests.

The native library is only loaded to satisfy the import; every test that
exercises behaviour monkeypatches ``canhub._native.lib``, so the real symbols
are never called. When no library is found the canhub tests are skipped.
"""

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
        for path in glob.glob(str(pattern), recursive=True):
            os.environ["CANHUB_LIBRARY"] = path
            return True
    return False


if not _ensure_library():
    collect_ignore_glob = ["test_*.py"]
    warnings.warn("libcanhub.so not found; skipping canhub tests (build with make release)")
