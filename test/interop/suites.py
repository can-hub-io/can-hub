"""TLS 1.3 suites this build offers: AES only where the CPU implements it."""

import re
from pathlib import Path

AES_SUITES = ["TLS_AES_128_GCM_SHA256", "TLS_AES_256_GCM_SHA384"]
CHACHA_SUITE = "TLS_CHACHA20_POLY1305_SHA256"


def offered_suites() -> list[str]:
    if cpu_has_hardware_aes():
        return AES_SUITES + [CHACHA_SUITE]
    return [CHACHA_SUITE]


def cpu_has_hardware_aes() -> bool:
    flags = Path("/proc/cpuinfo").read_text()
    return re.search(r"^(flags|Features)\s*:.*\baes\b", flags, re.MULTILINE) is not None
