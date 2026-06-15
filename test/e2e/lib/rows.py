"""Parsed views of can-hub-cli and candump output."""

from __future__ import annotations

import re
from dataclasses import dataclass


@dataclass
class HubStatus:
    peers: int
    agents: int
    clients: int
    received: int
    forwarded: int
    dropped: int
    unroutable: int


@dataclass
class PeerRow:
    peer_id: int
    role: str
    agent: str
    forwarded: int
    dropped: int
    fingerprint: str


@dataclass
class ClientRow:
    peer_id: int
    channel: str
    interface_id: str
    agent: str
    interface: str
    forwarded: int
    dropped: int


@dataclass
class InterfaceRow:
    interface_id: int
    agent: str
    interface: str
    subscribers: int
    frames: int
    tx_dropped: int


@dataclass
class ListRow:
    interface_id: int
    agent: str
    interface: str


@dataclass
class Frame:
    timestamp: float
    interface: str
    can_id: str
    data: str


def parse_status(text: str) -> HubStatus:
    peers = re.search(r"peers:\s*(\d+)\s*\(agents\s*(\d+),\s*clients\s*(\d+)\)", text)
    frames = re.search(
        r"frames:\s*received\s*(\d+),\s*forwarded\s*(\d+),\s*dropped\s*(\d+),\s*unroutable\s*(\d+)",
        text,
    )
    if not peers or not frames:
        raise ValueError(f"unparsable status:\n{text}")
    return HubStatus(
        peers=int(peers.group(1)),
        agents=int(peers.group(2)),
        clients=int(peers.group(3)),
        received=int(frames.group(1)),
        forwarded=int(frames.group(2)),
        dropped=int(frames.group(3)),
        unroutable=int(frames.group(4)),
    )


def _data_rows(text: str) -> list[list[str]]:
    rows = []
    for line in text.splitlines():
        fields = line.split()
        if not fields or fields[0].endswith("-id") or fields[0] in ("peers:", "frames:"):
            continue
        rows.append(fields)
    return rows


def parse_peers(text: str) -> list[PeerRow]:
    rows = []
    for f in _data_rows(text):
        if len(f) < 6:
            continue
        rows.append(PeerRow(int(f[0], 16), f[1], f[2], int(f[3]), int(f[4]), f[5]))
    return rows


def parse_clients(text: str) -> list[ClientRow]:
    """Tolerant of both the 5-column layout and the 7-column per-channel one
    (forwarded/dropped counters, added in #59)."""
    rows = []
    for f in _data_rows(text):
        if len(f) < 5 or not f[0].startswith("0x"):
            continue
        forwarded = int(f[5]) if len(f) >= 7 else 0
        dropped = int(f[6]) if len(f) >= 7 else 0
        rows.append(ClientRow(int(f[0], 16), f[1], f[2], f[3], f[4], forwarded, dropped))
    return rows


def parse_interfaces(text: str) -> list[InterfaceRow]:
    rows = []
    for f in _data_rows(text):
        if len(f) < 6 or not f[0].isdigit():
            continue
        rows.append(InterfaceRow(int(f[0]), f[1], f[2], int(f[3]), int(f[4]), int(f[5])))
    return rows


def parse_list(text: str) -> list[ListRow]:
    rows = []
    for f in _data_rows(text):
        if len(f) < 3 or not f[0].isdigit():
            continue
        rows.append(ListRow(int(f[0]), f[1], f[2]))
    return rows


def parse_candump(text: str) -> list[Frame]:
    frames = []
    for line in text.splitlines():
        match = re.match(r"\(([\d.]+)\)\s+(\S+)\s+([0-9A-Fa-f]+)#([0-9A-Fa-f]*)", line)
        if match:
            frames.append(
                Frame(float(match.group(1)), match.group(2), match.group(3).upper(), match.group(4).upper())
            )
    return frames


def parse_client_dump(text: str) -> list[Frame]:
    """can-hub-client dump: ``(1780847295.078524) 123 [4] DE AD BE EF``."""
    frames = []
    for line in text.splitlines():
        match = re.match(r"\(([\d.]+)\)\s+([0-9A-Fa-f]+)\s+\[\d+\]\s*([0-9A-Fa-f ]*)", line)
        if match:
            data = match.group(3).replace(" ", "").upper()
            frames.append(Frame(float(match.group(1)), "", match.group(2).upper(), data))
    return frames
