"""Latency / delivery / CPU metrics, ported from spike/e0-quic-datagram.

Latency is one-way, correlated by identical payload across a single candump
clock that watches both the source and the destination bus.
"""

from __future__ import annotations

from dataclasses import dataclass

from .rows import parse_candump


@dataclass
class LatencyReport:
    sent: int
    received: int
    p50_us: float
    p99_us: float
    max_us: float

    @property
    def delivery_percent(self) -> float:
        return 100.0 * self.received / self.sent if self.sent else 0.0


def correlate(candump_log: str, source_interface: str, dest_interface: str) -> LatencyReport:
    sent: dict[str, float] = {}
    deltas: list[float] = []
    for frame in parse_candump(candump_log):
        key = f"{frame.can_id}#{frame.data}"
        if frame.interface == source_interface:
            sent[key] = frame.timestamp
        elif frame.interface == dest_interface and key in sent:
            deltas.append((frame.timestamp - sent[key]) * 1e6)
    deltas.sort()

    def percentile(p: float) -> float:
        if not deltas:
            return 0.0
        return deltas[min(len(deltas) - 1, int(len(deltas) * p))]

    return LatencyReport(
        sent=len(sent),
        received=len(deltas),
        p50_us=percentile(0.50),
        p99_us=percentile(0.99),
        max_us=deltas[-1] if deltas else 0.0,
    )


def cpu_percent(jiffies_before: int, jiffies_after: int, elapsed_s: float,
                hz: float = 100.0) -> float:
    if elapsed_s <= 0:
        return 0.0
    return (jiffies_after - jiffies_before) / hz / elapsed_s * 100.0
