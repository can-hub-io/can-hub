"""Transactions: request/response sequences the agent runs at the bus.

A transaction moves the gating loop to the agent, so each exchange costs a
bus round trip instead of a network one (UDS flashing, broadcast upgrades;
see doc/protocol.md "Transactions"). Open the bus with ``reliable=True``::

    with bus.transaction(roster=range(32), origin=Origin(mask=0x1F), exclusive=True) as txn:
        txn.template(0, Template(advance_on=Pattern(0x700, can_mask=0x7E0), mode=Advance.ROSTER_ALL))
        txn.send(steps)           # any iterable, a generator included
        result = txn.wait()       # raises TransactionAborted with the report

Payload values are bytes: a pattern matches the first 8 payload bytes under
``data_mask`` (``None`` = every byte given in ``data``).
"""

import ctypes
import itertools
import time
from dataclasses import dataclass
from enum import IntEnum
from typing import FrozenSet, Iterable, Iterator, Optional

from can import CanOperationError, Message

from . import _native as native

PATTERN_PAYLOAD_BYTES = 8
SEND_CHUNK_STEPS = 256
SEND_SLICE_MS = 50
WAIT_SLICE_MS = 50
CLOSE_WAIT_MS = 1000
MICROSECONDS_PER_SECOND = 1_000_000
MILLISECONDS_PER_SECOND = 1000
STANDARD_ID_MASK = 0x7FF

REASONS = (
    "none",
    "timeout",
    "abort_match",
    "value_mismatch",
    "tx_failed",
    "cancelled",
    "starved",
    "overrun",
    "agent_disconnected",
    "invalid_step",
)


class Advance(IntEnum):
    """How a step waits once its frame is sent."""

    NONE = 0
    COUNT = 1
    ROSTER_ALL = 2
    ROSTER_K = 3


@dataclass(frozen=True)
class Pattern:
    """A frame the step waits for: id under ``can_mask`` and payload bytes.

    ``can_mask`` ``None`` compares every id bit and the extended flag.
    """

    arbitration_id: int
    can_mask: Optional[int] = None
    data: bytes = b""
    data_mask: Optional[bytes] = None
    is_extended_id: bool = False


@dataclass(frozen=True)
class Origin:
    """How to tell who answered: ``(X & mask) >> shift``, X being the CAN id,
    or the 4 payload bytes at ``payload_offset`` (little-endian) when given."""

    mask: int
    shift: int = 0
    payload_offset: Optional[int] = None


@dataclass(frozen=True)
class Template:
    """How a step waits: up to 16 per transaction, each defined once."""

    advance_on: Optional[Pattern] = None
    abort_on: Optional[Pattern] = None
    extend_on: Optional[Pattern] = None
    mode: Advance = Advance.NONE
    count: int = 1
    timeout: float = 1.0
    gap: float = 0.0
    soft: bool = False
    abort_on_mismatch: bool = False


@dataclass(frozen=True)
class Step:
    """One step: sends a frame (expect-only when ``arbitration_id`` is None),
    then waits as its template says. ``expect`` replaces the bytes the
    template's advance pattern expects, for this step only."""

    template: int = 0
    arbitration_id: Optional[int] = None
    data: bytes = b""
    is_extended_id: bool = False
    is_fd: bool = False
    bitrate_switch: bool = False
    expect: Optional[bytes] = None


@dataclass(frozen=True)
class TransactionProgress:
    sent_steps: int
    completed_steps: int
    bytes_in_flight: int
    window_bytes: int


@dataclass(frozen=True)
class TransactionResult:
    """The agent's report. ``answered``/``missing`` resolve the roster of the
    failing step; ``frame`` is the frame that triggered the failure, if any."""

    done: bool
    reason: str
    failing_step: int
    completed_steps: int
    answered: FrozenSet[int]
    missing: FrozenSet[int]
    offending_origin: int
    expected: bytes
    got: bytes
    frame: Optional[Message]


class TransactionAborted(CanOperationError):
    """The transaction ended before its last step; ``result`` says why."""

    def __init__(self, result: TransactionResult):
        super().__init__(f"transaction aborted at step {result.failing_step}: {result.reason}")
        self.result = result


class Transaction:
    """A transaction granted on one bus; use as a context manager so an
    exception or Ctrl-C cancels it at the agent."""

    def __init__(self, bus, roster: Iterable[int]):
        self._bus = bus
        self._roster = tuple(roster)
        self._ended = False
        self._finished = False

    def __enter__(self) -> "Transaction":
        return self

    def __exit__(self, exception_type, exception, traceback) -> None:
        if self._finished:
            return
        if exception_type is None:
            self.wait()
            return
        with self._bus._lock:
            if native.lib.canhub_txn_abort(self._bus._session) == native.OK:
                native.lib.canhub_txn_wait(self._bus._session, None, CLOSE_WAIT_MS)
        self._finished = True

    def template(self, template_id: int, template: Template, timeout: Optional[float] = None) -> None:
        definition = to_native_template(template_id, template)
        with self._bus._lock:
            result = native.lib.canhub_txn_template(self._bus._session, ctypes.byref(definition), _timeout_ms(timeout))
        self._check(result)

    def send(self, steps: Iterable[Step]) -> int:
        """Hand every step over, waiting for the agent's window as needed, the
        way a blocking write does; returns how many were handed over."""
        sent = 0

        for chunk in _chunks(steps, SEND_CHUNK_STEPS):
            native_steps = (native.CanHubTxnStep * len(chunk))(*[to_native_step(step) for step in chunk])
            self._send_chunk(native_steps, len(chunk))
            sent += len(chunk)
        return sent

    def end(self, timeout: Optional[float] = None) -> None:
        """Mark the last step handed over as the last one."""
        with self._bus._lock:
            result = native.lib.canhub_txn_end(self._bus._session, _timeout_ms(timeout))
        self._check(result)
        self._ended = True

    def wait(self, timeout: Optional[float] = None) -> TransactionResult:
        """Wait for the outcome (ends the transaction first if not done yet)."""
        deadline = None if timeout is None else time.monotonic() + max(0.0, timeout)
        report = native.CanHubTxnResult()
        report.struct_size = ctypes.sizeof(report)

        if not self._ended:
            self.end(timeout)
        while True:
            with self._bus._lock:
                status = native.lib.canhub_txn_wait(self._bus._session, ctypes.byref(report), _slice_ms(deadline, WAIT_SLICE_MS))
            if status != native.ERR_TIMEOUT:
                break
            if _expired(deadline):
                raise CanOperationError("transaction still running")

        self._finished = True
        result = from_native_result(report, self._roster)
        if status == native.ERR_TXN_ABORTED:
            raise TransactionAborted(result)
        if status != native.OK:
            raise CanOperationError(native.last_error(self._bus._session))
        return result

    def abort(self) -> None:
        """Cancel at the agent; wait() then reports the reason cancelled."""
        with self._bus._lock:
            result = native.lib.canhub_txn_abort(self._bus._session)
        self._check(result)
        self._ended = True

    def progress(self) -> TransactionProgress:
        progress = native.CanHubTxnProgress()
        progress.struct_size = ctypes.sizeof(progress)
        with self._bus._lock:
            native.lib.canhub_txn_progress(self._bus._session, ctypes.byref(progress))
        return TransactionProgress(
            progress.sent_steps, progress.completed_steps, progress.bytes_in_flight, progress.window_bytes
        )

    def _send_chunk(self, native_steps, count: int) -> None:
        step_size = ctypes.sizeof(native.CanHubTxnStep)
        taken = 0

        while taken < count:
            remaining = ctypes.cast(ctypes.byref(native_steps, taken * step_size), ctypes.POINTER(native.CanHubTxnStep))
            with self._bus._lock:
                result = native.lib.canhub_txn_send(self._bus._session, remaining, count - taken, SEND_SLICE_MS)
            self._check(result)
            taken += result

    def _check(self, result: int) -> None:
        if result == native.ERR_TXN_ABORTED:
            raise TransactionAborted(self._collect_result())
        if result < native.OK:
            raise CanOperationError(native.last_error(self._bus._session))

    def _collect_result(self) -> TransactionResult:
        report = native.CanHubTxnResult()
        report.struct_size = ctypes.sizeof(report)
        with self._bus._lock:
            native.lib.canhub_txn_wait(self._bus._session, ctypes.byref(report), 0)
        self._finished = True
        return from_native_result(report, self._roster)


def begin(bus, roster: Iterable[int], origin: Optional[Origin], exclusive: bool, timeout: float) -> Transaction:
    members = tuple(roster)
    request = to_native_begin(members, origin, exclusive)
    with bus._lock:
        result = native.lib.canhub_txn_begin(bus._session, ctypes.byref(request), int(timeout * MILLISECONDS_PER_SECOND))
    if result != native.OK:
        raise CanOperationError(native.last_error(bus._session))
    return Transaction(bus, members)


def to_native_begin(roster, origin: Optional[Origin], exclusive: bool) -> "native.CanHubTxnBegin":
    if len(roster) > native.TXN_ROSTER_MAX:
        raise ValueError(f"a roster holds at most {native.TXN_ROSTER_MAX} origins")
    if roster and origin is None:
        raise ValueError("a roster needs an origin to tell who answered")

    request = native.CanHubTxnBegin()
    request.struct_size = ctypes.sizeof(request)
    request.flags = native.TXN_FLAG_EXCLUSIVE if exclusive else 0
    if origin is not None:
        request.origin_source = native.TXN_ORIGIN_CAN_ID if origin.payload_offset is None else native.TXN_ORIGIN_PAYLOAD
        request.origin_shift = origin.shift
        request.origin_payload_offset = origin.payload_offset or 0
        request.origin_mask = origin.mask
    request.roster_count = len(roster)
    for index, member in enumerate(roster):
        request.roster[index] = member
    return request


def to_native_template(template_id: int, template: Template) -> "native.CanHubTxnTemplate":
    if not 0 <= template_id < native.TXN_TEMPLATES_MAX:
        raise ValueError(f"template ids go from 0 to {native.TXN_TEMPLATES_MAX - 1}")

    definition = native.CanHubTxnTemplate()
    definition.struct_size = ctypes.sizeof(definition)
    definition.template_id = template_id
    definition.advance_mode = int(template.mode)
    definition.count_k = template.count
    definition.timeout_ms = int(round(template.timeout * MILLISECONDS_PER_SECOND))
    definition.gap_us = int(round(template.gap * MICROSECONDS_PER_SECOND))
    if template.soft:
        definition.flags |= native.TXN_TEMPLATE_SOFT
    if template.abort_on_mismatch:
        definition.flags |= native.TXN_TEMPLATE_MISMATCH_ABORT
    for slot, pattern in (
        (native.TXN_PATTERN_ADVANCE, template.advance_on),
        (native.TXN_PATTERN_ABORT, template.abort_on),
        (native.TXN_PATTERN_EXTEND, template.extend_on),
    ):
        if pattern is not None:
            definition.patterns_present |= 1 << slot
            definition.patterns[slot] = to_native_pattern(pattern)
    return definition


def to_native_pattern(pattern: Pattern) -> "native.CanHubTxnPattern":
    native_pattern = native.CanHubTxnPattern()
    native_pattern.can_id = _wire_id(pattern.arbitration_id, pattern.is_extended_id)
    native_pattern.can_mask = _id_mask(pattern) if pattern.can_mask is None else pattern.can_mask
    native_pattern.payload_value = _payload_word(pattern.data)
    native_pattern.payload_mask = _payload_word(_data_mask(pattern))
    return native_pattern


def to_native_step(step: Step) -> "native.CanHubTxnStep":
    data = bytes(step.data)
    native_step = native.CanHubTxnStep()
    native_step.template_id = step.template
    if step.arbitration_id is None:
        native_step.flags |= native.TXN_STEP_EXPECT_ONLY
    else:
        native_step.can_id = _wire_id(step.arbitration_id, step.is_extended_id)
    if step.expect is not None:
        native_step.flags |= native.TXN_STEP_EXPECT_VALUE
        native_step.expect_value = _payload_word(step.expect)
    if step.is_fd:
        native_step.frame_flags |= native.FRAME_FLAG_FD
    if step.bitrate_switch:
        native_step.frame_flags |= native.FRAME_FLAG_BRS
    native_step.length = len(data)
    native_step.payload[: len(data)] = data
    return native_step


def from_native_result(report: "native.CanHubTxnResult", roster) -> TransactionResult:
    answered = frozenset(member for index, member in enumerate(roster) if report.seen_bitmap & (1 << index))
    frame = None
    if report.has_rx_frame:
        frame = Message(
            arbitration_id=report.rx_frame.can_id & native.CAN_ID_MASK,
            is_extended_id=bool(report.rx_frame.can_id & native.CAN_ID_FLAG_EFF),
            is_fd=bool(report.rx_frame.flags & native.FRAME_FLAG_FD),
            data=bytes(report.rx_frame.payload[: report.rx_frame.length]),
        )
    return TransactionResult(
        done=report.state == native.TXN_DONE and report.reason == 0,
        reason=REASONS[report.reason] if report.reason < len(REASONS) else str(report.reason),
        failing_step=report.failing_seq,
        completed_steps=report.completed_steps,
        answered=answered,
        missing=frozenset(roster) - answered,
        offending_origin=report.offending_origin,
        expected=report.expected.to_bytes(PATTERN_PAYLOAD_BYTES, "little"),
        got=report.got.to_bytes(PATTERN_PAYLOAD_BYTES, "little"),
        frame=frame,
    )


def _wire_id(arbitration_id: int, is_extended_id: bool) -> int:
    wire_id = arbitration_id & native.CAN_ID_MASK
    if is_extended_id:
        wire_id |= native.CAN_ID_FLAG_EFF
    return wire_id


def _id_mask(pattern: Pattern) -> int:
    id_bits = native.CAN_ID_MASK if pattern.is_extended_id else STANDARD_ID_MASK
    return id_bits | native.CAN_ID_FLAG_EFF


def _data_mask(pattern: Pattern) -> bytes:
    if pattern.data_mask is not None:
        return pattern.data_mask
    return b"\xff" * len(pattern.data)


def _payload_word(data: bytes) -> int:
    if len(data) > PATTERN_PAYLOAD_BYTES:
        raise ValueError(f"patterns compare the first {PATTERN_PAYLOAD_BYTES} payload bytes")
    return int.from_bytes(bytes(data).ljust(PATTERN_PAYLOAD_BYTES, b"\0"), "little")


def _chunks(steps: Iterable[Step], size: int) -> Iterator[list]:
    iterator = iter(steps)
    while True:
        chunk = list(itertools.islice(iterator, size))
        if not chunk:
            return
        yield chunk


def _timeout_ms(timeout: Optional[float]) -> int:
    return -1 if timeout is None else max(0, int(timeout * MILLISECONDS_PER_SECOND))


def _slice_ms(deadline: Optional[float], slice_ms: int) -> int:
    if deadline is None:
        return slice_ms
    remaining_ms = max(0, int((deadline - time.monotonic()) * MILLISECONDS_PER_SECOND))
    return min(slice_ms, remaining_ms)


def _expired(deadline: Optional[float]) -> bool:
    return deadline is not None and time.monotonic() >= deadline
