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
    receive_overflows: int
    transactions_completed: int = 0
    transactions_aborted: int = 0
    transaction_steps: int = 0


@dataclass
class TransactionRow:
    interface: str
    client: int
    state: str
    lease: str
    txn: int
    steps: int
    in_flight: int


@dataclass
class PeerRow:
    peer_id: int
    role: str
    via: str
    name: str
    origin: str
    uptime: str
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
    bridged: bool = False


def parse_status(text: str) -> HubStatus:
    peers = re.search(r"peers:\s*(\d+)\s*\(agents\s*(\d+),\s*clients\s*(\d+)\)", text)
    frames = re.search(
        r"frames:\s*received\s*(\d+),\s*forwarded\s*(\d+),\s*dropped\s*(\d+),\s*unroutable\s*(\d+)",
        text,
    )
    overflows = re.search(r"receive overflows:\s*(\d+)", text)
    transactions = re.search(r"transactions:\s*started\s*\d+,\s*completed\s*(\d+),\s*aborted\s*(\d+),.*steps\s*(\d+)", text)
    if not peers or not frames or not overflows:
        raise ValueError(f"unparsable status:\n{text}")
    return HubStatus(
        peers=int(peers.group(1)),
        agents=int(peers.group(2)),
        clients=int(peers.group(3)),
        received=int(frames.group(1)),
        forwarded=int(frames.group(2)),
        dropped=int(frames.group(3)),
        unroutable=int(frames.group(4)),
        receive_overflows=int(overflows.group(1)),
        transactions_completed=int(transactions.group(1)) if transactions else 0,
        transactions_aborted=int(transactions.group(2)) if transactions else 0,
        transaction_steps=int(transactions.group(3)) if transactions else 0,
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
        if len(f) < 9:
            continue
        rows.append(PeerRow(int(f[0], 16), f[1], f[2], f[3], f[4], f[5], int(f[6]), int(f[7]), f[8]))
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


def parse_transactions(text: str) -> list[TransactionRow]:
    rows = []
    for f in _data_rows(text):
        if len(f) < 7 or not f[1].startswith("0x"):
            continue
        rows.append(TransactionRow(f[0], int(f[1], 16), f[2], f[3], int(f[4]), int(f[5]), int(f[6])))
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


CANFD_FDF_FLAG = 0x04
FD_SEPARATOR = "##"


def parse_candump_specs(text: str) -> list[str]:
    """candump -L frames in cansend syntax (``123##1DEADBEEF``, ``123#R``), see normalise_spec."""
    return [normalise_spec(fields[2]) for fields in (line.split() for line in text.splitlines())
            if len(fields) == 3 and fields[0].startswith("(")]


def normalise_spec(spec: str) -> str:
    """Upper-case, and drop CANFD_FDF from FD flags: kernels >= 6.2 set it on every FD frame."""
    spec = spec.upper()
    if FD_SEPARATOR not in spec:
        return spec
    can_id, rest = spec.split(FD_SEPARATOR, 1)
    flags = int(rest[0], 16) & ~CANFD_FDF_FLAG
    return f"{can_id}{FD_SEPARATOR}{flags:X}{rest[1:]}"


def parse_client_dump(text: str) -> list[Frame]:
    """can-hub-client dump: ``(1780847295.078524) 123 [4] DE AD BE EF``."""
    frames = []
    for line in text.splitlines():
        match = re.match(r"\(([\d.]+)\)\s+([0-9A-Fa-f]+)\s+\[\d+\]\s*([0-9A-Fa-f ]*)", line)
        if match:
            data = match.group(3).replace(" ", "").upper()
            frames.append(Frame(float(match.group(1)), "", match.group(2).upper(), data, "(bridged)" in line))
    return frames
