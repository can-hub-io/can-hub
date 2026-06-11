"""python-can-hub: native python-can backend for can-hub.

The native library loads lazily: importing the package (e.g. for the pure
python fingerprint helper) must work without libcanhub present.
"""

from .fingerprint import identity_fingerprint

__all__ = ["CanHubBus", "identity_fingerprint"]


def __getattr__(name):
    if name == "CanHubBus":
        from .bus import CanHubBus
        return CanHubBus
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
