"""python-can-hub: native python-can backend for can-hub.

The native library loads lazily: importing the package (e.g. for the pure
python fingerprint helper) must work without libcanhub present.
"""

from .fingerprint import identity_fingerprint

__all__ = [
    "Advance",
    "BitTiming",
    "CanHubBus",
    "CanHubMessage",
    "InterfaceSettings",
    "Origin",
    "Pattern",
    "ScriptError",
    "Step",
    "Template",
    "Transaction",
    "TransactionAborted",
    "TransactionResult",
    "identity_fingerprint",
]

TRANSACTION_NAMES = {"Advance", "Origin", "Pattern", "ScriptError", "Step", "Template", "Transaction", "TransactionAborted", "TransactionResult"}


def __getattr__(name):
    if name == "CanHubBus":
        from .bus import CanHubBus
        return CanHubBus
    if name == "CanHubMessage":
        from .bus import CanHubMessage
        return CanHubMessage
    if name == "InterfaceSettings":
        from .settings import InterfaceSettings
        return InterfaceSettings
    if name == "BitTiming":
        from .settings import BitTiming
        return BitTiming
    if name in TRANSACTION_NAMES:
        from . import transaction
        return getattr(transaction, name)
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
